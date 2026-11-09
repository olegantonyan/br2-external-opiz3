// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"

#define BG_TOP 0x0b1016
#define BG_BOT 0x121b26
#define CARD 0x161f2b
#define BORDER 0x243244
#define TRACK 0x26344a
#define TEXT 0xe6edf3
#define DIM 0x8b98a8
#define FAINT 0x55657a
#define ACCENT 0x6cb6ff
#define GREEN 0x3fb950
#define AMBER 0xd29922
#define RED 0xf85149
#define TEAL 0x39c5cf
#define PURPLE 0xbc8cff

#define DEG "\xf8"
#define UP "\x18"
#define DOWN "\x19"
#define SEP " \xfa "

static int u, s_body, s_small, s_title, s_big, s_huge, pad, gap, rad;

struct box {
	int x, y, w, h, cy;
};

static uint32_t level(double v, double warn, double crit)
{
	return v >= crit ? RED : v >= warn ? AMBER : GREEN;
}

static void rate(char *b, size_t n, double v)
{
	if (v < 1000)
		snprintf(b, n, "%.0f B/s", v);
	else if (v < 1e6)
		snprintf(b, n, "%.1f kB/s", v / 1e3);
	else
		snprintf(b, n, "%.1f MB/s", v / 1e6);
}

static void size(char *b, size_t n, double v)
{
	if (v < 1e6)
		snprintf(b, n, "%.0f kB", v / 1e3);
	else if (v < 1e9)
		snprintf(b, n, "%.1f MB", v / 1e6);
	else
		snprintf(b, n, "%.1f GB", v / 1e9);
}

static uint32_t *base;
static int base_w, base_h;

static void card_frame(struct canvas *c, int x, int y, int w, int h, const char *title)
{
	round_rect(c, x, y, w, h, rad, BORDER);
	round_rect(c, x + 1, y + 1, w - 2, h - 2, rad - 1, CARD);
	text(c, x + pad, y + pad, s_title, ACCENT, title);
}

static struct box card(int x, int y, int w, int h)
{
	return (struct box){ x + pad, y + pad, w - 2 * pad, h - 2 * pad, y + pad + s_title + u / 2 };
}

void ui_reset(void)
{
	free(base);
	base = NULL;
	base_w = base_h = 0;
}

static int bottom(const struct box *b)
{
	return b->y + b->h;
}

static void headline(struct canvas *c, struct box *b, const char *big, uint32_t col, const char *l1, const char *l2)
{
	int x = text(c, b->x, b->cy, s_big, col, big) + u / 2;
	if (l1)
		text_fit(c, x, b->cy + (s_big - s_body - s_small) / 2, b->x + b->w - x, s_body, TEXT, l1);
	if (l2)
		text_fit(c, x, b->cy + (s_big + s_body - s_small) / 2, b->x + b->w - x, s_small, DIM, l2);
	b->cy += s_big + u / 2;
}

static void meter(struct canvas *c, struct box *b, const char *label, double frac, uint32_t col, const char *value)
{
	int lw = 6 * (s_small / 2), vw = 13 * (s_small / 2), bh = u / 3 > 4 ? u / 3 : 4;
	if (b->cy + s_small > bottom(b))
		return;
	text(c, b->x, b->cy, s_small, DIM, label);
	bar(c, b->x + lw, b->cy + (s_small - bh) / 2, b->w - lw - vw, bh, frac, col, TRACK);
	text_right(c, b->x + b->w, b->cy, s_small, TEXT, value);
	b->cy += s_small + u / 4;
}

static void history(struct canvas *c, struct box *b, const float *v, float max, uint32_t col, const char *note)
{
	int h = bottom(b) - b->cy;
	if (h < u)
		return;
	for (int i = 1; i < 4; i++)
		blend(c, b->x, b->cy + h * i / 4, b->w, 1, BORDER, 160);
	blend(c, b->x, bottom(b) - 1, b->w, 1, FAINT, 120);
	spark(c, b->x, b->cy, b->w, h, v, HIST, 0, max, col);
	if (note)
		text_right(c, b->x + b->w, b->cy, s_small, FAINT, note);
}

static void cpu_card(struct canvas *c, struct box b, const struct stats *s)
{
	char big[16], l1[48], l2[64], v[16];
	float h[HIST];
	snprintf(big, sizeof(big), "%.0f%%", s->cpu * 100);
	snprintf(l1, sizeof(l1), "%d / %d MHz", s->mhz, s->mhz_max);
	snprintf(l2, sizeof(l2), "load %.2f %.2f %.2f", s->load[0], s->load[1], s->load[2]);
	headline(c, &b, big, level(s->cpu * 100, 50, 85), l1, l2);
	for (int i = 0; i < s->ncpu; i++) {
		char l[8];
		snprintf(l, sizeof(l), "cpu%d", i);
		snprintf(v, sizeof(v), "%3.0f%%", s->core[i] * 100);
		meter(c, &b, l, s->core[i], level(s->core[i] * 100, 50, 85), v);
	}
	b.cy += u / 4;
	for (int i = 0; i < HIST; i++)
		h[i] = s->cpuh[(s->head + i) % HIST];
	history(c, &b, h, 100, ACCENT, "2 min");
}

static void mem_card(struct canvas *c, struct box b, const struct stats *s)
{
	char big[16], l1[48], l2[48], v[32];
	float h[HIST];
	long used = s->mem_total - s->mem_avail;
	double frac = s->mem_total ? (double)used / s->mem_total : 0;
	snprintf(big, sizeof(big), "%.0f%%", frac * 100);
	snprintf(l1, sizeof(l1), "%ld / %ld MB", used / 1024, s->mem_total / 1024);
	snprintf(l2, sizeof(l2), "available %ld MB", s->mem_avail / 1024);
	headline(c, &b, big, level(frac * 100, 70, 90), l1, l2);
	snprintf(v, sizeof(v), "%ld MB", used / 1024);
	meter(c, &b, "used", frac, level(frac * 100, 70, 90), v);
	snprintf(v, sizeof(v), "%ld MB", s->mem_cached / 1024);
	meter(c, &b, "cache", s->mem_total ? (double)s->mem_cached / s->mem_total : 0, PURPLE, v);
	snprintf(v, sizeof(v), "%ld/%ld MB", (s->cma_total - s->cma_free) / 1024, s->cma_total / 1024);
	meter(c, &b, "CMA", s->cma_total ? (double)(s->cma_total - s->cma_free) / s->cma_total : 0, TEAL, v);
	size(v, sizeof(v), s->root_used);
	meter(c, &b, "root", s->mem_total ? s->root_used / 1024.0 / s->mem_total : 0, ACCENT, v);
	b.cy += u / 4;
	for (int i = 0; i < HIST; i++)
		h[i] = s->memh[(s->head + i) % HIST];
	history(c, &b, h, 100, PURPLE, "2 min");
}

static void temp_card(struct canvas *c, struct box b, const struct stats *s)
{
	char big[16], v[16];
	float h[HIST];
	double max = 0;
	for (int i = 0; i < s->ntemp; i++)
		if (s->temp[i].c > max)
			max = s->temp[i].c;
	snprintf(big, sizeof(big), "%.0f" DEG "C", max);
	headline(c, &b, big, level(max, 65, 75), "hottest sensor", "throttles at 70" DEG "C");
	for (int i = 0; i < s->ntemp; i++) {
		snprintf(v, sizeof(v), "%.1f" DEG "C", s->temp[i].c);
		meter(c, &b, s->temp[i].name, (s->temp[i].c - 20) / 70, level(s->temp[i].c, 65, 75), v);
	}
	b.cy += u / 4;
	for (int i = 0; i < HIST; i++) {
		float t = s->temph[(s->head + i) % HIST] - 20;
		h[i] = t > 0 ? t : 0;
	}
	history(c, &b, h, 70, AMBER, "2 min");
}

static void link(struct canvas *c, struct box *b, const struct netif *n, const char *detail)
{
	char l[96], rx[24], tx[24];
	int r = s_body / 4;
	uint32_t col = !n->present ? FAINT : n->up ? GREEN : RED;
	dot(c, b->x + r, b->cy + s_body / 2, r, col);
	text(c, b->x + 3 * r, b->cy, s_body, TEXT, n->name);
	text_right(c, b->x + b->w, b->cy, s_body, n->ip[0] ? TEXT : FAINT, n->ip[0] ? n->ip : n->present ? "no address" : "absent");
	b->cy += s_body + u / 6;
	text_fit(c, b->x + 3 * r, b->cy, b->w - 3 * r, s_small, DIM, detail);
	b->cy += s_small + u / 6;
	rate(rx, sizeof(rx), n->rxr);
	rate(tx, sizeof(tx), n->txr);
	snprintf(l, sizeof(l), DOWN " %s", rx);
	text(c, b->x + 3 * r, b->cy, s_small, TEAL, l);
	snprintf(l, sizeof(l), UP " %s", tx);
	text(c, b->x + 3 * r + 14 * (s_small / 2), b->cy, s_small, PURPLE, l);
	b->cy += s_small + u / 6;
	float max = 1000;
	for (int i = 0; i < HIST; i++) {
		if (n->rxh[i] > max) max = n->rxh[i];
		if (n->txh[i] > max) max = n->txh[i];
	}
	int sh = u * 3 / 2;
	if (b->cy + sh <= bottom(b)) {
		blend(c, b->x + 3 * r, b->cy + sh - 1, b->w - 3 * r, 1, FAINT, 120);
		spark(c, b->x + 3 * r, b->cy, b->w - 3 * r, sh, n->rxh, HIST, 0, max, TEAL);
		spark(c, b->x + 3 * r, b->cy, b->w - 3 * r, sh, n->txh, HIST, 0, max, PURPLE);
	}
	b->cy += sh + u / 2;
}

static void net_card(struct canvas *c, struct box b, const struct stats *s)
{
	char d[96];
	struct netif eth = s->eth, wlan = s->wlan;
	for (int i = 0; i < HIST; i++) {
		eth.rxh[i] = s->eth.rxh[(s->head + i) % HIST];
		eth.txh[i] = s->eth.txh[(s->head + i) % HIST];
		wlan.rxh[i] = s->wlan.rxh[(s->head + i) % HIST];
		wlan.txh[i] = s->wlan.txh[(s->head + i) % HIST];
	}
	if (s->eth.speed > 0)
		snprintf(d, sizeof(d), "%ld Mb/s", s->eth.speed);
	else
		snprintf(d, sizeof(d), "%s", s->eth.present ? "no link" : "");
	link(c, &b, &eth, d);
	if (s->ssid[0])
		snprintf(d, sizeof(d), "%s" SEP "%d dBm" SEP "%d Mb/s" SEP "%.1f GHz", s->ssid, s->rssi, s->linkspeed, s->freq / 1000.0);
	else
		snprintf(d, sizeof(d), "%s", s->wlan.present ? "not associated" : "");
	link(c, &b, &wlan, d);
	if (b.cy + s_small <= bottom(&b)) {
		int r = s_body / 4;
		dot(c, b.x + r, b.cy + s_small / 2, r, !s->bt_present ? FAINT : s->bt_on ? GREEN : RED);
		text(c, b.x + 3 * r, b.cy, s_small, TEXT, "Bluetooth");
		text_right(c, b.x + b.w, b.cy, s_small, DIM, !s->bt_present ? "absent" : s->bt_on ? "hci0 on" : "hci0 blocked");
	}
}

static void audio_card(struct canvas *c, struct box b, const struct stats *s)
{
	char l[96];
	int r = s_body / 4;
	for (int i = 0; i < s->ncard && b.cy + s_body <= bottom(&b); i++) {
		const typeof(s->card[0]) *a = &s->card[i];
		int running = !strcmp(a->state, "RUNNING"), closed = !strcmp(a->state, "closed");
		uint32_t col = running ? GREEN : closed ? FAINT : AMBER;
		dot(c, b.x + r, b.cy + s_body / 2, r, col);
		text(c, b.x + 3 * r, b.cy, s_body, TEXT, a->id);
		text_right(c, b.x + b.w, b.cy, s_small, col == FAINT ? DIM : col, running ? "playing" : closed ? "idle" : a->state);
		b.cy += s_body + u / 6;
		if (a->name[0] && strcmp(a->name, a->id)) {
			text_fit(c, b.x + 3 * r, b.cy, b.w - 3 * r, s_small, FAINT, a->name);
			b.cy += s_small + u / 6;
		}
		if (closed) {
			if (a->last[0]) {
				long ago = s->uptime - a->last_t;
				char when[24];
				if (ago < 60)
					snprintf(when, sizeof(when), "just now");
				else if (ago < 3600)
					snprintf(when, sizeof(when), "%ld min ago", ago / 60);
				else
					snprintf(when, sizeof(when), "%ld h ago", ago / 3600);
				snprintf(l, sizeof(l), "last %s, %s", a->last, when);
				text_fit(c, b.x + 3 * r, b.cy, b.w - 3 * r, s_small, DIM, l);
				b.cy += s_small + u / 6;
			}
			b.cy += u / 3;
			continue;
		}
		snprintf(l, sizeof(l), "%d Hz" SEP "%s" SEP "%d ch", a->rate, a->fmt, a->ch);
		text_fit(c, b.x + 3 * r, b.cy, b.w - 3 * r, s_small, GREEN, l);
		b.cy += s_small + u / 6;
		if (a->owner[0]) {
			snprintf(l, sizeof(l), "by %s", a->owner);
			text_fit(c, b.x + 3 * r, b.cy, b.w - 3 * r, s_small, DIM, l);
			b.cy += s_small + u / 6;
		}
		b.cy += u / 3;
	}
	if (!s->ncard)
		text(c, b.x, b.cy, s_small, DIM, "no sound cards");
}

static void storage_card(struct canvas *c, struct box b, const struct stats *s)
{
	char l[64], used[16], tot[16];
	int bh = u / 3 > 4 ? u / 3 : 4;
	for (int i = 0; i < s->nmnt && b.cy + s_body + bh <= bottom(&b); i++) {
		const char *name = strrchr(s->mnt[i].dir, '/') + 1;
		size(used, sizeof(used), s->mnt[i].used);
		size(tot, sizeof(tot), s->mnt[i].size);
		snprintf(l, sizeof(l), "%s" SEP "%s / %s", s->mnt[i].fs, used, tot);
		text(c, b.x, b.cy, s_body, TEXT, name);
		text_right(c, b.x + b.w, b.cy + (s_body - s_small), s_small, DIM, l);
		b.cy += s_body + u / 6;
		double f = s->mnt[i].size ? (double)s->mnt[i].used / s->mnt[i].size : 0;
		bar(c, b.x, b.cy, b.w, bh, f, level(f * 100, 80, 95), TRACK);
		b.cy += bh + u / 2;
	}
	if (!s->nmnt) {
		text(c, b.x, b.cy, s_small, DIM, "no SD card or USB drive");
		b.cy += s_small + u / 2;
	}
	if (b.cy + s_title > bottom(&b))
		return;
	text(c, b.x, b.cy, s_title, ACCENT, "USB");
	b.cy += s_title + u / 3;
	for (int i = 0; i < s->nusb && b.cy + s_small <= bottom(&b); i++) {
		snprintf(l, sizeof(l), "%d Mb/s", s->usb[i].mbps);
		text_fit(c, b.x, b.cy, b.w - 10 * (s_small / 2), s_small, TEXT, s->usb[i].name);
		text_right(c, b.x + b.w, b.cy, s_small, DIM, l);
		b.cy += s_small + u / 6;
	}
	if (!s->nusb)
		text(c, b.x, b.cy, s_small, DIM, "no devices");
}

static void header(struct canvas *c, int x, int y, int w, const struct stats *s)
{
	char l[128], hm[8], sec[8], date[48];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(hm, sizeof(hm), "%H:%M", &tm);
	strftime(sec, sizeof(sec), ":%S", &tm);
	strftime(date, sizeof(date), "%A, %d %B %Y", &tm);

	text(c, x, y + (s_huge - s_big) / 2, s_big, TEXT, s->host);
	if (s->uptime >= 86400)
		snprintf(l, sizeof(l), "Linux %s" SEP "up %ld d %ld h", s->kernel, s->uptime / 86400, s->uptime / 3600 % 24);
	else
		snprintf(l, sizeof(l), "Linux %s" SEP "up %ld h %02ld min", s->kernel, s->uptime / 3600, s->uptime / 60 % 60);
	text(c, x, y + s_huge + u / 4, s_small, DIM, l);

	int sx = x + w - text_width(s_big, sec);
	text(c, sx, y + s_huge - s_big - s_big / 16, s_big, DIM, sec);
	text_right(c, sx, y, s_huge, TEXT, hm);
	int dw = text_width(s_small, date);
	text(c, x + w - dw, y + s_huge + u / 4, s_small, DIM, date);
	int r = s_small / 4, dx = x + w - dw - u / 2 - text_width(s_small, s->ntp_sync ? "NTP" : "clock not synced") - 3 * r;
	dot(c, dx + r, y + s_huge + u / 4 + s_small / 2, r, s->ntp_sync ? GREEN : AMBER);
	text(c, dx + 3 * r, y + s_huge + u / 4, s_small, s->ntp_sync ? DIM : AMBER, s->ntp_sync ? "NTP" : "clock not synced");
}

static void footer(struct canvas *c, int x, int y, int w, const struct uiinfo *ui)
{
	char l[160];
	int kw = text_width(s_small, " Esc "), kh = s_small + u / 6;
	round_rect(c, x, y - u / 12, kw, kh, u / 6, ui->keyboards ? TRACK : CARD);
	text(c, x, y, s_small, ui->keyboards ? TEXT : FAINT, " Esc ");
	text(c, x + kw + u / 3, y, s_small, ui->keyboards ? DIM : FAINT, ui->keyboards ? "console login" : "console login (no keyboard)");
	snprintf(l, sizeof(l), "%dx%d@%d" SEP "%s" SEP "frame %.0f ms" SEP "hotplug %u" SEP "modeset %u%s", ui->w, ui->h, ui->hz,
		 ui->monitor, ui->frame_ms, ui->hotplugs, ui->modesets, ui->failures ? SEP "FAILURES" : "");
	text_right(c, x + w, y, s_small, ui->failures ? AMBER : FAINT, l);
}

void ui_draw(struct canvas *c, const struct stats *s, const struct uiinfo *ui)
{
	int W = c->w, H = c->h;
	u = W / 64 < H / 36 ? W / 64 : H / 36;
	if (u < 10)
		u = 10;
	s_body = u * 4 / 5;
	s_small = u * 13 / 20;
	s_title = u * 3 / 5;
	s_big = u * 8 / 5;
	s_huge = u * 12 / 5;
	pad = u * 3 / 5;
	gap = u / 2;
	rad = u * 2 / 5;
	int m = u * 3 / 4;

	int top = m + s_huge + u / 4 + s_small + gap + u / 4;
	int fy = H - m - s_small;
	int cw = (W - 2 * m - 2 * gap) / 3, ch = (fy - gap - top - gap) / 2;
	int x[3] = { m, m + cw + gap, m + 2 * (cw + gap) }, y[2] = { top, top + ch + gap };
	static const char *titles[6] = { "CPU", "MEMORY", "TEMPERATURE", "NETWORK", "AUDIO", "STORAGE" };

	if (!base || base_w != W || base_h != H) {
		ui_reset();
		base = malloc((size_t)W * H * 4);
		if (!base)
			return;
		base_w = W;
		base_h = H;
		struct canvas b = { base, W, H };
		vgradient(&b, 0, 0, W, H, BG_TOP, BG_BOT);
		for (int i = 0; i < 6; i++)
			card_frame(&b, x[i % 3], y[i / 3], cw, ch, titles[i]);
	}
	memcpy(c->px, base, (size_t)W * H * 4);

	header(c, m, m, W - 2 * m, s);
	cpu_card(c, card(x[0], y[0], cw, ch), s);
	mem_card(c, card(x[1], y[0], cw, ch), s);
	temp_card(c, card(x[2], y[0], cw, ch), s);
	net_card(c, card(x[0], y[1], cw, ch), s);
	audio_card(c, card(x[1], y[1], cw, ch), s);
	storage_card(c, card(x[2], y[1], cw, ch), s);
	footer(c, m, fy, W - 2 * m, ui);
}
