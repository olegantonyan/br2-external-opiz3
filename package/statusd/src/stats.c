// SPDX-License-Identifier: GPL-2.0
#include <ctype.h>
#include <dirent.h>
#include <glob.h>
#include <ifaddrs.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/timex.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include "stats.h"

#define WPA_CTRL "/var/run/wpa_supplicant/wlan0"

static int wpa = -1;
static char wpa_local[64];

static int read_line(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int ok = fgets(buf, n, f) != NULL;
	fclose(f);
	if (!ok)
		return -1;
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

static long read_long(const char *path, long def)
{
	char b[32];
	return read_line(path, b, sizeof(b)) ? def : strtol(b, NULL, 10);
}

static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void cpu(struct stats *s)
{
	static unsigned long long prev_busy[MAXCPU + 1], prev_total[MAXCPU + 1];
	char line[256];
	FILE *f = fopen("/proc/stat", "r");
	if (!f)
		return;
	int n = 0;
	while (fgets(line, sizeof(line), f) && !strncmp(line, "cpu", 3)) {
		unsigned long long v[8] = { 0 }, total = 0;
		int idx = line[3] == ' ' ? 0 : atoi(line + 3) + 1;
		if (idx > MAXCPU)
			continue;
		sscanf(strchr(line, ' '), "%llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
		for (int i = 0; i < 8; i++)
			total += v[i];
		unsigned long long busy = total - v[3] - v[4];
		double frac = total > prev_total[idx] ? (double)(busy - prev_busy[idx]) / (total - prev_total[idx]) : 0;
		prev_busy[idx] = busy;
		prev_total[idx] = total;
		if (idx == 0)
			s->cpu = frac;
		else
			s->core[idx - 1] = frac, n = idx;
	}
	fclose(f);
	s->ncpu = n;
	s->cpuh[s->head] = s->cpu * 100;
	s->mhz = read_long("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 0) / 1000;
	s->mhz_max = read_long("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", 0) / 1000;
	f = fopen("/proc/loadavg", "r");
	if (f) {
		if (fscanf(f, "%lf %lf %lf", &s->load[0], &s->load[1], &s->load[2]) != 3)
			s->load[0] = s->load[1] = s->load[2] = 0;
		fclose(f);
	}
}

static void mem(struct stats *s)
{
	char k[64];
	long v;
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return;
	while (fscanf(f, "%63s %ld kB\n", k, &v) == 2) {
		if (!strcmp(k, "MemTotal:")) s->mem_total = v;
		else if (!strcmp(k, "MemAvailable:")) s->mem_avail = v;
		else if (!strcmp(k, "Cached:")) s->mem_cached = v;
		else if (!strcmp(k, "CmaTotal:")) s->cma_total = v;
		else if (!strcmp(k, "CmaFree:")) s->cma_free = v;
	}
	fclose(f);
	struct statvfs vfs;
	s->root_used = statvfs("/", &vfs) ? 0 : (uint64_t)(vfs.f_blocks - vfs.f_bfree) * vfs.f_frsize;
	s->memh[s->head] = s->mem_total ? 100.0 * (s->mem_total - s->mem_avail) / s->mem_total : 0;
}

static void temps(struct stats *s)
{
	char path[64], type[32];
	double max = 0;
	s->ntemp = 0;
	for (int i = 0; i < 8 && s->ntemp < 8; i++) {
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);
		if (read_line(path, type, sizeof(type)))
			break;
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
		long t = read_long(path, -1000000);
		if (t <= -1000000)
			continue;
		char *dash = strchr(type, '-');
		if (dash)
			*dash = 0;
		for (char *p = type; *p; p++)
			*p = toupper((unsigned char)*p);
		snprintf(s->temp[s->ntemp].name, sizeof(s->temp[0].name), "%s", type);
		s->temp[s->ntemp].c = t / 1000.0;
		if (s->temp[s->ntemp].c > max)
			max = s->temp[s->ntemp].c;
		s->ntemp++;
	}
	s->temph[s->head] = max;
}

static void netif(struct stats *s, struct netif *n, double dt)
{
	char path[96], b[32];
	snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", n->name);
	n->present = !read_line(path, b, sizeof(b));
	n->up = n->present && !strcmp(b, "up");
	snprintf(path, sizeof(path), "/sys/class/net/%s/speed", n->name);
	n->speed = n->up ? read_long(path, -1) : -1;
	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes", n->name);
	uint64_t rx = read_long(path, 0);
	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes", n->name);
	uint64_t tx = read_long(path, 0);
	n->rxr = dt > 0 && n->rx && rx >= n->rx ? (rx - n->rx) / dt : 0;
	n->txr = dt > 0 && n->tx && tx >= n->tx ? (tx - n->tx) / dt : 0;
	n->rx = rx;
	n->tx = tx;
	n->rxh[s->head] = n->rxr;
	n->txh[s->head] = n->txr;
}

static void addrs(struct stats *s)
{
	struct ifaddrs *ifa;
	s->eth.ip[0] = s->wlan.ip[0] = 0;
	if (getifaddrs(&ifa))
		return;
	for (struct ifaddrs *i = ifa; i; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
			continue;
		struct netif *n = !strcmp(i->ifa_name, s->eth.name) ? &s->eth : !strcmp(i->ifa_name, s->wlan.name) ? &s->wlan : NULL;
		if (n)
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, n->ip, sizeof(n->ip));
	}
	freeifaddrs(ifa);
}

static int wpa_cmd(const char *cmd, char *reply, size_t n)
{
	if (wpa < 0) {
		struct sockaddr_un local = { .sun_family = AF_UNIX }, remote = { .sun_family = AF_UNIX };
		wpa = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
		if (wpa < 0)
			return -1;
		snprintf(wpa_local, sizeof(wpa_local), "/tmp/statusd-wpa-%d", getpid());
		unlink(wpa_local);
		snprintf(local.sun_path, sizeof(local.sun_path), "%s", wpa_local);
		snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", WPA_CTRL);
		if (bind(wpa, (struct sockaddr *)&local, sizeof(local)) || connect(wpa, (struct sockaddr *)&remote, sizeof(remote))) {
			stats_close();
			return -1;
		}
	}
	char junk[256];
	while (recv(wpa, junk, sizeof(junk), MSG_DONTWAIT) > 0)
		;
	if (send(wpa, cmd, strlen(cmd), 0) < 0) {
		stats_close();
		return -1;
	}
	struct pollfd p = { wpa, POLLIN, 0 };
	if (poll(&p, 1, 200) <= 0)
		return -1;
	ssize_t r = recv(wpa, reply, n - 1, 0);
	if (r <= 0)
		return -1;
	reply[r] = 0;
	return 0;
}

static const char *kv(const char *buf, const char *key)
{
	size_t k = strlen(key);
	for (const char *p = buf; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL)
		if (!strncmp(p, key, k) && p[k] == '=')
			return p + k + 1;
	return NULL;
}

static void wifi(struct stats *s)
{
	char r[2048];
	const char *v;
	s->ssid[0] = 0;
	s->rssi = s->linkspeed = s->freq = 0;
	if (!s->wlan.up || wpa_cmd("STATUS", r, sizeof(r)))
		return;
	if ((v = kv(r, "ssid")))
		snprintf(s->ssid, sizeof(s->ssid), "%.*s", (int)strcspn(v, "\n"), v);
	if (!wpa_cmd("SIGNAL_POLL", r, sizeof(r))) {
		if ((v = kv(r, "RSSI"))) s->rssi = atoi(v);
		if ((v = kv(r, "LINKSPEED"))) s->linkspeed = atoi(v);
		if ((v = kv(r, "FREQUENCY"))) s->freq = atoi(v);
	}
}

static void field(const char *path, const char *key, char *out, size_t n)
{
	char line[128];
	size_t k = strlen(key);
	FILE *f = fopen(path, "r");
	out[0] = 0;
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, k))
			continue;
		char *v = line + k;
		while (*v == ' ' || *v == ':')
			v++;
		v[strcspn(v, " \n")] = 0;
		snprintf(out, n, "%s", v);
		break;
	}
	fclose(f);
}

static void card_name(int idx, char *out, size_t n)
{
	char line[160];
	FILE *f = fopen("/proc/asound/cards", "r");
	out[0] = 0;
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *dash = strstr(line, "]: ");
		if (atoi(line) != idx || !dash || !(dash = strstr(dash, " - ")))
			continue;
		dash[3 + strcspn(dash + 3, "\n")] = 0;
		snprintf(out, n, "%s", dash + 3);
		break;
	}
	fclose(f);
}

static void audio(struct stats *s)
{
	glob_t g;
	typeof(s->card) old;
	int nold = s->ncard;
	memcpy(old, s->card, sizeof(old));
	s->ncard = 0;
	if (glob("/proc/asound/card[0-9]*/pcm*p/sub0/status", 0, NULL, &g))
		return;
	for (size_t i = 0; i < g.gl_pathc && s->ncard < 4; i++) {
		char base[96], path[128], b[32];
		snprintf(base, sizeof(base), "%.*s", (int)(strstr(g.gl_pathv[i], "/pcm") - g.gl_pathv[i]), g.gl_pathv[i]);
		typeof(s->card[0]) *c = &s->card[s->ncard++];
		memset(c, 0, sizeof(*c));
		snprintf(path, sizeof(path), "%s/id", base);
		if (read_line(path, c->id, sizeof(c->id)))
			snprintf(c->id, sizeof(c->id), "?");
		card_name(atoi(base + strlen("/proc/asound/card")), c->name, sizeof(c->name));
		for (int k = 0; k < nold; k++)
			if (!strcmp(old[k].id, c->id)) {
				memcpy(c->last, old[k].last, sizeof(c->last));
				c->last_t = old[k].last_t;
			}
		if (read_line(g.gl_pathv[i], b, sizeof(b)) || !strcmp(b, "closed")) {
			snprintf(c->state, sizeof(c->state), "closed");
			continue;
		}
		field(g.gl_pathv[i], "state", c->state, sizeof(c->state));
		field(g.gl_pathv[i], "owner_pid", b, sizeof(b));
		snprintf(path, sizeof(path), "/proc/%s/comm", b);
		if (!b[0] || read_line(path, c->owner, sizeof(c->owner)))
			c->owner[0] = 0;
		snprintf(path, sizeof(path), "%.*s/hw_params", (int)(strrchr(g.gl_pathv[i], '/') - g.gl_pathv[i]), g.gl_pathv[i]);
		field(path, "format", c->fmt, sizeof(c->fmt));
		field(path, "rate", b, sizeof(b));
		c->rate = atoi(b);
		field(path, "channels", b, sizeof(b));
		c->ch = atoi(b);
		if (!strcmp(c->state, "RUNNING")) {
			char last[sizeof(c->last)];
			snprintf(last, sizeof(last), "%d Hz \xfa %s \xfa %d ch", c->rate, c->fmt, c->ch);
			memcpy(c->last, last, sizeof(last));
			c->last_t = s->uptime;
		}
	}
	globfree(&g);
}

static void mounts(struct stats *s)
{
	char dev[128], dir[128], fs[32], rest[256];
	s->nmnt = 0;
	FILE *f = fopen("/proc/mounts", "r");
	if (!f)
		return;
	while (s->nmnt < 8 && fscanf(f, "%127s %127s %31s %255[^\n]\n", dev, dir, fs, rest) == 4) {
		if (strcmp(dir, "/mnt/nvm") && strcmp(dir, "/mnt/sdcard") && strncmp(dir, "/media/", 7))
			continue;
		struct statvfs v;
		if (statvfs(dir, &v))
			continue;
		typeof(s->mnt[0]) *m = &s->mnt[s->nmnt++];
		snprintf(m->dir, sizeof(m->dir), "%s", dir);
		snprintf(m->fs, sizeof(m->fs), "%s", fs);
		m->size = (uint64_t)v.f_blocks * v.f_frsize;
		m->used = (uint64_t)(v.f_blocks - v.f_bfree) * v.f_frsize;
	}
	fclose(f);
}

static void usb(struct stats *s)
{
	DIR *d = opendir("/sys/bus/usb/devices");
	struct dirent *e;
	s->nusb = 0;
	if (!d)
		return;
	while ((e = readdir(d)) && s->nusb < 8) {
		char path[300], b[64], vid[8], pid[8];
		if (e->d_name[0] == '.' || strchr(e->d_name, ':') || !strncmp(e->d_name, "usb", 3))
			continue;
		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/bDeviceClass", e->d_name);
		if (!read_line(path, b, sizeof(b)) && !strcmp(b, "09"))
			continue;
		typeof(s->usb[0]) *u = &s->usb[s->nusb++];
		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/product", e->d_name);
		if (read_line(path, u->name, sizeof(u->name))) {
			snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", e->d_name);
			if (read_line(path, vid, sizeof(vid)))
				vid[0] = 0;
			snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct", e->d_name);
			if (read_line(path, pid, sizeof(pid)))
				pid[0] = 0;
			snprintf(u->name, sizeof(u->name), "%s:%s", vid, pid);
		}
		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/speed", e->d_name);
		u->mbps = read_long(path, 0);
	}
	closedir(d);
}

static void bt(struct stats *s)
{
	glob_t g;
	s->bt_present = !access("/sys/class/bluetooth/hci0", F_OK);
	s->bt_on = 0;
	if (glob("/sys/class/rfkill/rfkill*", 0, NULL, &g))
		return;
	for (size_t i = 0; i < g.gl_pathc; i++) {
		char path[96], b[32];
		snprintf(path, sizeof(path), "%s/name", g.gl_pathv[i]);
		if (read_line(path, b, sizeof(b)) || strcmp(b, "hci0"))
			continue;
		snprintf(path, sizeof(path), "%s/state", g.gl_pathv[i]);
		s->bt_on = read_long(path, 0) == 1;
	}
	globfree(&g);
}

void stats_init(struct stats *s)
{
	struct utsname u;
	memset(s, 0, sizeof(*s));
	s->eth.name = "eth0";
	s->wlan.name = "wlan0";
	if (!uname(&u))
		snprintf(s->kernel, sizeof(s->kernel), "%s", u.release);
}

void stats_update(struct stats *s)
{
	static double last;
	double t = now_s(), dt = last ? t - last : 0;
	struct sysinfo si;
	struct timex tx = { 0 };
	last = t;

	if (!sysinfo(&si))
		s->uptime = si.uptime;
	cpu(s);
	mem(s);
	temps(s);
	netif(s, &s->eth, dt);
	netif(s, &s->wlan, dt);
	addrs(s);
	wifi(s);
	audio(s);
	mounts(s);
	usb(s);
	bt(s);
	gethostname(s->host, sizeof(s->host) - 1);
	int st = adjtimex(&tx);
	s->ntp_sync = st != TIME_ERROR && !(tx.status & STA_UNSYNC);
	if (!s->filled) {
		for (int i = 0; i < HIST; i++) {
			s->memh[i] = s->memh[s->head];
			s->temph[i] = s->temph[s->head];
		}
		s->filled = 1;
	}
	s->head = (s->head + 1) % HIST;
}

void stats_close(void)
{
	if (wpa >= 0) {
		close(wpa);
		wpa = -1;
		unlink(wpa_local);
	}
}
