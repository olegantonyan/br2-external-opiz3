// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "stats.h"
#include "ui.h"

#define CARD "/dev/dri/card0"
#define MAX_W 2560
#define MAX_H 1440
#define MAX_HZ 60
#define DEBOUNCE_MS 500
#define RECHECK_S 30
#define FLIP_TIMEOUT_S 2
#define MAXIN 16

struct buf {
	uint32_t handle, fb, pitch;
	uint64_t size;
	uint8_t *map;
};

static struct {
	int fd;
	uint32_t conn, crtc;
	int connected, active, full;
	drmModeModeInfo mode;
	uint64_t cfg;
	char monitor[16];
	struct buf buf[2];
	int front, flip_pending;
	uint32_t gen;
	time_t flip_ts;
	uint32_t *shadow;
	unsigned hotplugs, modesets, failures;
	double frame_ms;
	pid_t console;
} d = { .fd = -1 };

static struct {
	int fd;
	dev_t rdev;
} in[MAXIN];
static int nin, esc_down;

static struct stats st;

static void msg(int prio, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsyslog(prio, fmt, ap);
	va_end(ap);
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = p;
	while (n--)
		h = (h ^ *b++) * 0x100000001b3ULL;
	return h;
}

static double mono_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int open_card(void)
{
	int fd = open(CARD, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (drmSetMaster(fd)) {
		close(fd);
		return -1;
	}
	drmModeRes *res = drmModeGetResources(fd);
	if (!res) {
		close(fd);
		return -1;
	}
	uint32_t conn = 0;
	for (int i = 0; i < res->count_connectors && !conn; i++) {
		drmModeConnector *c = drmModeGetConnectorCurrent(fd, res->connectors[i]);
		if (c && c->connector_type == DRM_MODE_CONNECTOR_HDMIA)
			conn = c->connector_id;
		drmModeFreeConnector(c);
	}
	if (!conn && res->count_connectors)
		conn = res->connectors[0];
	drmModeFreeResources(res);
	if (!conn) {
		close(fd);
		return -1;
	}
	d.fd = fd;
	d.conn = conn;
	return 0;
}

static uint32_t pick_crtc(drmModeConnector *c)
{
	drmModeRes *res = drmModeGetResources(d.fd);
	uint32_t crtc = 0;
	for (int i = 0; res && i < c->count_encoders && !crtc; i++) {
		drmModeEncoder *e = drmModeGetEncoder(d.fd, c->encoders[i]);
		if (!e)
			continue;
		if (e->crtc_id)
			crtc = e->crtc_id;
		for (int j = 0; j < res->count_crtcs && !crtc; j++)
			if (e->possible_crtcs & (1u << j))
				crtc = res->crtcs[j];
		drmModeFreeEncoder(e);
	}
	drmModeFreeResources(res);
	return crtc;
}

static drmModePropertyBlobRes *edid_blob(drmModeConnector *c)
{
	for (int i = 0; i < c->count_props; i++) {
		drmModePropertyRes *p = drmModeGetProperty(d.fd, c->props[i]);
		int match = p && !strcmp(p->name, "EDID");
		drmModeFreeProperty(p);
		if (match)
			return c->prop_values[i] ? drmModeGetPropertyBlob(d.fd, c->prop_values[i]) : NULL;
	}
	return NULL;
}

static void edid_name(const uint8_t *e, uint32_t len, char *out)
{
	strcpy(out, len ? "unnamed" : "no EDID");
	for (int i = 54; len >= 128 && i <= 108; i += 18) {
		if (e[i] || e[i + 1] || e[i + 2] || e[i + 3] != 0xfc)
			continue;
		int n = 0;
		while (n < 13 && e[i + 5 + n] != '\n') {
			out[n] = e[i + 5 + n];
			n++;
		}
		while (n && out[n - 1] == ' ')
			n--;
		out[n] = 0;
	}
}

static long mode_rank(const drmModeModeInfo *m)
{
	if (m->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN))
		return -1;
	if (m->type & DRM_MODE_TYPE_USERDEF)
		return 3L << 40;
	if (m->hdisplay > MAX_W || m->vdisplay > MAX_H || m->vrefresh > MAX_HZ)
		return -1;
	long r = (long)m->hdisplay * m->vdisplay << 8 | m->vrefresh;
	if (m->type & DRM_MODE_TYPE_PREFERRED)
		r |= 2L << 40;
	return r;
}

static int cmp_rank(const void *a, const void *b)
{
	long ra = mode_rank(a), rb = mode_rank(b);
	return (rb > ra) - (rb < ra);
}

static void free_buf(struct buf *b)
{
	if (b->map)
		munmap(b->map, b->size);
	if (b->fb)
		drmModeRmFB(d.fd, b->fb);
	if (b->handle)
		drmModeDestroyDumbBuffer(d.fd, b->handle);
	memset(b, 0, sizeof(*b));
}

static int alloc_buf(struct buf *b, int w, int h)
{
	uint64_t off;
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	memset(b, 0, sizeof(*b));
	if (drmModeCreateDumbBuffer(d.fd, w, h, 32, 0, &b->handle, &b->pitch, &b->size))
		return -1;
	handles[0] = b->handle;
	pitches[0] = b->pitch;
	if (drmModeAddFB2(d.fd, w, h, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b->fb, 0) ||
	    drmModeMapDumbBuffer(d.fd, b->handle, &off))
		goto fail;
	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, d.fd, off);
	if (b->map == MAP_FAILED) {
		b->map = NULL;
		goto fail;
	}
	return 0;
fail:
	free_buf(b);
	return -1;
}

static void render(struct buf *b)
{
	double t = mono_ms();
	struct canvas c = { d.shadow, d.mode.hdisplay, d.mode.vdisplay };
	struct uiinfo ui = { d.mode.hdisplay, d.mode.vdisplay, d.mode.vrefresh, nin, d.monitor, d.hotplugs, d.modesets, d.failures, d.frame_ms };
	ui_draw(&c, &st, &ui);
	for (int y = 0; y < c.h; y++)
		memcpy(b->map + (size_t)y * b->pitch, d.shadow + (size_t)y * c.w, (size_t)c.w * 4);
	d.frame_ms = mono_ms() - t;
}

static void release_display(void)
{
	free_buf(&d.buf[0]);
	free_buf(&d.buf[1]);
	free(d.shadow);
	d.shadow = NULL;
	draw_reset();
	ui_reset();
	if (d.fd >= 0)
		close(d.fd);
	d.fd = -1;
	d.active = 0;
	d.flip_pending = 0;
	d.gen++;
}

static int apply(drmModeModeInfo *mode)
{
	struct buf nb[2];
	drmModeModeInfo old = d.mode;
	uint32_t *sh = malloc((size_t)mode->hdisplay * mode->vdisplay * 4);

	if (!sh)
		return -1;
	if (alloc_buf(&nb[0], mode->hdisplay, mode->vdisplay)) {
		msg(LOG_WARNING, "%s: buffer 0: %m", mode->name);
		free(sh);
		return -1;
	}
	if (alloc_buf(&nb[1], mode->hdisplay, mode->vdisplay)) {
		msg(LOG_WARNING, "%s: buffer 1: %m", mode->name);
		free_buf(&nb[0]);
		free(sh);
		return -1;
	}
	uint32_t *oldsh = d.shadow;
	d.shadow = sh;
	d.mode = *mode;
	if (old.hdisplay != mode->hdisplay || old.vdisplay != mode->vdisplay)
		draw_reset();
	render(&nb[0]);
	if (d.full && drmModeSetCrtc(d.fd, d.crtc, 0, 0, 0, NULL, 0, NULL))
		msg(LOG_WARNING, "disable CRTC %u: %m", d.crtc);
	if (drmModeSetCrtc(d.fd, d.crtc, nb[0].fb, 0, 0, &d.conn, 1, mode)) {
		msg(LOG_WARNING, "%s@%d: setcrtc: %m", mode->name, mode->vrefresh);
		d.shadow = oldsh;
		d.mode = old;
		free(sh);
		free_buf(&nb[0]);
		free_buf(&nb[1]);
		return -1;
	}
	free(oldsh);
	free_buf(&d.buf[0]);
	free_buf(&d.buf[1]);
	d.buf[0] = nb[0];
	d.buf[1] = nb[1];
	d.front = 0;
	d.flip_pending = 0;
	d.gen++;
	d.active = 1;
	d.full = 0;
	d.modesets++;
	return 0;
}

static int crtc_ours(void)
{
	drmModeCrtc *c = drmModeGetCrtc(d.fd, d.crtc);
	int ok = c && c->mode_valid && (c->buffer_id == d.buf[0].fb || c->buffer_id == d.buf[1].fb);
	drmModeFreeCrtc(c);
	return ok;
}

static void reconcile(void)
{
	if (d.fd < 0)
		return;
	drmModeConnector *c = drmModeGetConnector(d.fd, d.conn);
	if (!c) {
		msg(LOG_ERR, "get connector %u: %m", d.conn);
		return;
	}
	int connected = c->connection != DRM_MODE_DISCONNECTED && c->count_modes > 0;
	if (connected != d.connected) {
		msg(LOG_INFO, "HDMI %s", connected ? "connected" : "disconnected");
		if (connected && d.active) {
			d.active = 0;
			d.full = 1;
		}
	}
	d.connected = connected;

	if (d.active && !crtc_ours()) {
		msg(LOG_WARNING, "CRTC %u no longer shows our framebuffer, re-applying", d.crtc);
		d.active = 0;
		d.full = 1;
	}
	if (!connected) {
		drmModeFreeConnector(c);
		return;
	}

	drmModePropertyBlobRes *edid = edid_blob(c);
	char name[16];
	edid_name(edid ? edid->data : NULL, edid ? edid->length : 0, name);
	qsort(c->modes, c->count_modes, sizeof(*c->modes), cmp_rank);
	uint64_t cfg = fnv(0xcbf29ce484222325ULL, edid ? edid->data : "", edid ? edid->length : 0);
	cfg = fnv(cfg, c->modes, sizeof(*c->modes) * c->count_modes);
	drmModeFreePropertyBlob(edid);

	if (d.active && cfg == d.cfg) {
		drmModeFreeConnector(c);
		return;
	}
	if (!d.crtc)
		d.crtc = pick_crtc(c);
	if (!d.crtc) {
		msg(LOG_ERR, "no CRTC for connector %u", d.conn);
		d.failures++;
		drmModeFreeConnector(c);
		return;
	}
	strcpy(d.monitor, name);
	int ok = 0;
	for (int i = 0; i < c->count_modes && !ok; i++) {
		if (mode_rank(&c->modes[i]) < 0)
			break;
		ok = !apply(&c->modes[i]);
	}
	if (ok) {
		d.cfg = cfg;
		msg(LOG_INFO, "modeset %ux%u@%u on \"%s\" (cfg %016llx)", d.mode.hdisplay, d.mode.vdisplay, d.mode.vrefresh, d.monitor,
		    (unsigned long long)cfg);
	} else {
		d.failures++;
		msg(LOG_ERR, "no usable mode among %d on \"%s\"", c->count_modes, name);
	}
	drmModeFreeConnector(c);
}

static void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
	(void)fd, (void)seq, (void)sec, (void)usec;
	if ((uintptr_t)data != d.gen)
		return;
	d.front ^= 1;
	d.flip_pending = 0;
}

static void redraw(void)
{
	if (!d.active)
		return;
	if (d.flip_pending) {
		if (time(NULL) - d.flip_ts < FLIP_TIMEOUT_S)
			return;
		msg(LOG_WARNING, "page flip timed out, re-applying");
		d.failures++;
		d.flip_pending = 0;
		d.active = 0;
		d.full = 1;
		reconcile();
		return;
	}
	struct buf *back = &d.buf[d.front ^ 1];
	render(back);
	if (drmModePageFlip(d.fd, d.crtc, back->fb, DRM_MODE_PAGE_FLIP_EVENT, (void *)(uintptr_t)d.gen)) {
		if (errno != EBUSY) {
			msg(LOG_WARNING, "page flip: %m, re-applying");
			d.active = 0;
			d.full = 1;
			reconcile();
		}
		return;
	}
	d.flip_pending = 1;
	d.flip_ts = time(NULL);
}

static void screenshot(void)
{
	const char *path = "/tmp/statusd.ppm";
	int W = d.mode.hdisplay, H = d.mode.vdisplay;
	FILE *f;
	if (!d.shadow || !(f = fopen(path, "w")))
		return;
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (size_t i = 0; i < (size_t)W * H; i++) {
		uint32_t p = d.shadow[i];
		uint8_t rgb[3] = { p >> 16, p >> 8, p };
		fwrite(rgb, 3, 1, f);
	}
	fclose(f);
	msg(LOG_INFO, "screenshot %s", path);
}

static void grab_all(int on)
{
	for (int i = 0; i < nin; i++)
		if (ioctl(in[i].fd, EVIOCGRAB, on) && on)
			msg(LOG_WARNING, "grab input %d: %m", i);
}

static void drop_input(int i)
{
	close(in[i].fd);
	in[i] = in[--nin];
}

static void scan_inputs(void)
{
	DIR *dir = opendir("/dev/input");
	struct dirent *e;
	if (!dir)
		return;
	while ((e = readdir(dir)) && nin < MAXIN) {
		char path[300];
		struct stat sb;
		unsigned long keys[KEY_CNT / (8 * sizeof(long)) + 1] = { 0 };
		int known = 0;
		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		if (stat(path, &sb) || !S_ISCHR(sb.st_mode))
			continue;
		for (int i = 0; i < nin; i++)
			known |= in[i].rdev == sb.st_rdev;
		if (known)
			continue;
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		char name[64] = "?";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0 ||
		    !(keys[KEY_ESC / (8 * sizeof(long))] & (1UL << (KEY_ESC % (8 * sizeof(long)))))) {
			close(fd);
			continue;
		}
		if (!d.console && ioctl(fd, EVIOCGRAB, 1))
			msg(LOG_WARNING, "grab %s: %m", path);
		in[nin].fd = fd;
		in[nin].rdev = sb.st_rdev;
		nin++;
		msg(LOG_INFO, "keyboard %s \"%s\"", path, name);
	}
	closedir(dir);
}

static void enter_console(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		msg(LOG_ERR, "fork: %m");
		return;
	}
	if (pid == 0) {
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		signal(SIGPIPE, SIG_DFL);
		int t = open("/dev/tty1", O_WRONLY | O_NOCTTY);
		if (t >= 0) {
			if (write(t, "\033[H\033[2J", 7) < 0)
				_exit(1);
			close(t);
		}
		execl("/sbin/getty", "getty", "-t", "60", "38400", "tty1", (char *)NULL);
		_exit(127);
	}
	d.console = pid;
	esc_down = 0;
	grab_all(0);
	release_display();
	msg(LOG_INFO, "console login on tty1 (getty pid %d)", pid);
}

static void leave_console(void)
{
	d.console = 0;
	if (open_card()) {
		msg(LOG_WARNING, "reopen %s: %m, retrying", CARD);
		return;
	}
	grab_all(1);
	msg(LOG_INFO, "console session ended, status screen back");
	reconcile();
}

static void read_input(int i)
{
	struct input_event ev[32];
	ssize_t n;
	while ((n = read(in[i].fd, ev, sizeof(ev))) > 0)
		for (size_t k = 0; k < n / sizeof(ev[0]); k++) {
			if (d.console || ev[k].type != EV_KEY || ev[k].code != KEY_ESC)
				continue;
			if (ev[k].value == 1)
				esc_down = 1;
			else if (ev[k].value == 0 && esc_down)
				enter_console();
		}
	if (n < 0 && errno != EAGAIN && errno != EINTR) {
		msg(LOG_INFO, "keyboard gone (%m)");
		drop_input(i);
	}
}

static int uevent_socket(void)
{
	struct sockaddr_nl a = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	int sz = 1 << 20;
	int s = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
	if (s < 0)
		return -1;
	setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &sz, sizeof(sz));
	if (bind(s, (struct sockaddr *)&a, sizeof(a))) {
		close(s);
		return -1;
	}
	return s;
}

enum { EV_DRM = 1, EV_INPUT = 2 };

static int uevents(int s)
{
	char b[8192];
	int hit = 0;
	ssize_t n;
	while ((n = recv(s, b, sizeof(b) - 1, 0)) != 0) {
		if (n < 0) {
			if (errno == ENOBUFS)
				hit |= EV_DRM | EV_INPUT;
			if (errno == EINTR || errno == ENOBUFS)
				continue;
			break;
		}
		b[n] = 0;
		int drm = 0, hp = 0, input = 0;
		for (char *p = b; p < b + n; p += strlen(p) + 1) {
			drm |= !strcmp(p, "SUBSYSTEM=drm");
			hp |= !strcmp(p, "HOTPLUG=1");
			input |= !strcmp(p, "SUBSYSTEM=input");
		}
		hit |= (drm && hp ? EV_DRM : 0) | (input ? EV_INPUT : 0);
	}
	return hit;
}

static void arm(int tfd, long ms)
{
	struct itimerspec it = { .it_value = { ms / 1000, ms % 1000 * 1000000L } };
	timerfd_settime(tfd, 0, &it, NULL);
}

int main(int argc, char **argv)
{
	openlog("statusd", LOG_PID | (argc > 1 && !strcmp(argv[1], "-v") ? LOG_PERROR : 0), LOG_DAEMON);
	signal(SIGPIPE, SIG_IGN);

	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGHUP);
	sigaddset(&ss, SIGCHLD);
	sigaddset(&ss, SIGUSR1);
	sigaddset(&ss, SIGUSR2);
	sigprocmask(SIG_BLOCK, &ss, NULL);
	int sfd = signalfd(-1, &ss, SFD_CLOEXEC);

	int nl = uevent_socket();
	if (nl < 0)
		msg(LOG_ERR, "uevent socket: %m (falling back to %d s polling)", RECHECK_S);

	for (int tries = 0; open_card(); tries++) {
		if (tries % 30 == 0)
			msg(LOG_WARNING, "%s not usable yet (%m), retrying", CARD);
		struct signalfd_siginfo si;
		struct pollfd p = { sfd, POLLIN, 0 };
		if (poll(&p, 1, 2000) > 0 && read(sfd, &si, sizeof(si)) == sizeof(si) && (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT))
			return 0;
	}
	msg(LOG_INFO, "started on %s connector %u", CARD, d.conn);

	stats_init(&st);
	stats_update(&st);
	scan_inputs();

	int tick = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	struct itimerspec it = { { 1, 0 }, { 1, 0 } };
	timerfd_settime(tick, 0, &it, NULL);
	int deb = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	int pending = 0;

	reconcile();

	drmEventContext ev = { .version = 2, .page_flip_handler = on_flip };
	unsigned ticks = 0;
	for (;;) {
		struct pollfd p[5 + MAXIN] = {
			{ sfd, POLLIN, 0 }, { nl, POLLIN, 0 }, { tick, POLLIN, 0 }, { deb, POLLIN, 0 }, { d.fd, POLLIN, 0 },
		};
		int nfd = 5;
		for (int i = 0; i < nin; i++)
			p[nfd++] = (struct pollfd){ in[i].fd, POLLIN, 0 };
		if (poll(p, nfd, -1) < 0) {
			if (errno == EINTR)
				continue;
			msg(LOG_ERR, "poll: %m");
			return 1;
		}
		uint64_t x;
		if (p[4].revents & POLLIN && d.fd >= 0)
			drmHandleEvent(d.fd, &ev);
		for (int i = nin - 1; i >= 0; i--)
			if (p[5 + i].revents && p[5 + i].fd == in[i].fd)
				read_input(i);
		if (p[1].revents & POLLIN) {
			int e = uevents(nl);
			if (e & EV_DRM)
				d.hotplugs++;
			if (e) {
				pending |= e;
				arm(deb, DEBOUNCE_MS);
			}
		}
		if (p[3].revents & POLLIN && read(deb, &x, sizeof(x)) == sizeof(x)) {
			if (pending & EV_INPUT)
				scan_inputs();
			if (pending & EV_DRM && !d.console)
				reconcile();
			pending = 0;
		}
		if (p[2].revents & POLLIN && read(tick, &x, sizeof(x)) == sizeof(x)) {
			stats_update(&st);
			if (!d.console && d.fd < 0)
				leave_console();
			if (++ticks % RECHECK_S == 0) {
				scan_inputs();
				if (!d.console)
					reconcile();
			}
			if (!d.console)
				redraw();
		}
		if (p[0].revents & POLLIN) {
			struct signalfd_siginfo si;
			if (read(sfd, &si, sizeof(si)) != sizeof(si))
				continue;
			if (si.ssi_signo == SIGCHLD) {
				pid_t pid;
				while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
					if (pid == d.console)
						leave_console();
			} else if (si.ssi_signo == SIGUSR1) {
				screenshot();
			} else if (si.ssi_signo == SIGUSR2) {
				if (!d.console)
					enter_console();
			} else if (si.ssi_signo == SIGHUP) {
				msg(LOG_INFO, "SIGHUP: forcing full modeset");
				d.active = 0;
				d.full = 1;
				reconcile();
			} else {
				msg(LOG_INFO, "exiting on signal %u", si.ssi_signo);
				stats_close();
				return 0;
			}
		}
	}
}
