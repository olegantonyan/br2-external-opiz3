// SPDX-License-Identifier: GPL-2.0
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "font.h"

#define NSIZES 8

static struct glyphs {
	int h, w;
	uint8_t *a;
} cache[NSIZES];

void draw_reset(void)
{
	for (int i = 0; i < NSIZES; i++) {
		free(cache[i].a);
		cache[i] = (struct glyphs){ 0 };
	}
}

static struct glyphs *glyphs(int h)
{
	int i;
	for (i = 0; i < NSIZES && cache[i].h; i++)
		if (cache[i].h == h)
			return &cache[i];
	if (i == NSIZES) {
		draw_reset();
		i = 0;
	}
	struct glyphs *g = &cache[i];
	int w = h / 2;
	g->a = calloc(256, (size_t)w * h);
	if (!g->a)
		return NULL;
	g->h = h;
	g->w = w;
	for (int ch = 0; ch < 256; ch++) {
		const uint8_t *src = font16x32 + ch * 64;
		uint8_t *dst = g->a + (size_t)ch * w * h;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				int cov = 0;
				for (int sy = 0; sy < 4; sy++) {
					int ry = (int)((y + (sy + 0.5) / 4) * 32 / h);
					unsigned row = src[2 * ry] << 8 | src[2 * ry + 1];
					for (int sx = 0; sx < 4; sx++) {
						int rx = (int)((x + (sx + 0.5) / 4) * 16 / w);
						cov += !!(row & (0x8000 >> rx));
					}
				}
				dst[y * w + x] = cov * 255 / 16;
			}
	}
	return g;
}

static inline uint32_t mix(uint32_t d, uint32_t s, int a)
{
	uint32_t rb = ((s & 0xff00ff) * a + (d & 0xff00ff) * (255 - a)) >> 8 & 0xff00ff;
	uint32_t g = ((s & 0xff00) * a + (d & 0xff00) * (255 - a)) >> 8 & 0xff00;
	return rb | g;
}

static int clip(struct canvas *c, int *x, int *y, int *w, int *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > c->w) *w = c->w - *x;
	if (*y + *h > c->h) *h = c->h - *y;
	return *w > 0 && *h > 0;
}

void fill(struct canvas *c, int x, int y, int w, int h, uint32_t col)
{
	if (!clip(c, &x, &y, &w, &h))
		return;
	for (int j = 0; j < h; j++) {
		uint32_t *p = c->px + (size_t)(y + j) * c->w + x;
		for (int i = 0; i < w; i++)
			p[i] = col;
	}
}

void blend(struct canvas *c, int x, int y, int w, int h, uint32_t col, int alpha)
{
	if (!clip(c, &x, &y, &w, &h))
		return;
	for (int j = 0; j < h; j++) {
		uint32_t *p = c->px + (size_t)(y + j) * c->w + x;
		for (int i = 0; i < w; i++)
			p[i] = mix(p[i], col, alpha);
	}
}

static inline void plot(struct canvas *c, int x, int y, uint32_t col, int a)
{
	if (x < 0 || y < 0 || x >= c->w || y >= c->h || a <= 0)
		return;
	uint32_t *p = c->px + (size_t)y * c->w + x;
	*p = a >= 255 ? col : mix(*p, col, a);
}

void vgradient(struct canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
	for (int j = 0; j < h; j++)
		fill(c, x, y + j, w, 1, mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

static int coverage(double dx, double dy, double r)
{
	double v = r - sqrt(dx * dx + dy * dy) + 0.5;
	return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255);
}

void round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col)
{
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	fill(c, x, y + r, w, h - 2 * r, col);
	fill(c, x + r, y, w - 2 * r, r, col);
	fill(c, x + r, y + h - r, w - 2 * r, r, col);
	for (int j = 0; j < r; j++)
		for (int i = 0; i < r; i++) {
			int a = coverage(r - (i + 0.5), r - (j + 0.5), r);
			plot(c, x + i, y + j, col, a);
			plot(c, x + w - 1 - i, y + j, col, a);
			plot(c, x + i, y + h - 1 - j, col, a);
			plot(c, x + w - 1 - i, y + h - 1 - j, col, a);
		}
}

void dot(struct canvas *c, int cx, int cy, int r, uint32_t col)
{
	for (int j = -r - 1; j <= r; j++)
		for (int i = -r - 1; i <= r; i++)
			plot(c, cx + i, cy + j, col, coverage(i + 0.5, j + 0.5, r));
}

void bar(struct canvas *c, int x, int y, int w, int h, double frac, uint32_t fg, uint32_t bg)
{
	round_rect(c, x, y, w, h, h / 2, bg);
	if (frac <= 0)
		return;
	int fw = (int)(w * (frac > 1 ? 1 : frac));
	round_rect(c, x, y, fw < h ? h : fw, h, h / 2, fg);
}

void spark(struct canvas *c, int x, int y, int w, int h, const float *v, int n, int head, float max, uint32_t col)
{
	if (max <= 0 || w < 2)
		return;
	int prev = -1;
	for (int i = 0; i < w; i++) {
		double pos = (double)i * (n - 1) / (w - 1);
		int k = (int)pos;
		double f = pos - k;
		float a = v[(head + k) % n], b = v[(head + (k + 1 < n ? k + 1 : k)) % n];
		double val = a + (b - a) * f;
		if (val > max)
			val = max;
		int top = y + h - 1 - (int)(val / max * (h - 1));
		blend(c, x + i, top, 1, y + h - top, col, 60);
		int from = prev < 0 ? top : prev < top ? prev : top, to = prev < 0 ? top : prev > top ? prev : top;
		blend(c, x + i, from - 1, 1, to - from + 2, col, 255);
		prev = top;
	}
}

int text_width(int size, const char *s)
{
	return (int)strlen(s) * (size / 2);
}

int text(struct canvas *c, int x, int y, int size, uint32_t col, const char *s)
{
	struct glyphs *g = glyphs(size);
	if (!g)
		return x;
	for (; *s; s++, x += g->w) {
		const uint8_t *a = g->a + (size_t)(uint8_t)*s * g->w * g->h;
		for (int j = 0; j < g->h; j++)
			for (int i = 0; i < g->w; i++)
				if (a[j * g->w + i])
					plot(c, x + i, y + j, col, a[j * g->w + i]);
	}
	return x;
}

int text_right(struct canvas *c, int right, int y, int size, uint32_t col, const char *s)
{
	return text(c, right - text_width(size, s), y, size, col, s);
}

int text_fit(struct canvas *c, int x, int y, int maxw, int size, uint32_t col, const char *s)
{
	int n = size / 2 ? maxw / (size / 2) : 0;
	if (n <= 0)
		return x;
	if ((int)strlen(s) <= n)
		return text(c, x, y, size, col, s);
	char buf[256];
	if (n > (int)sizeof(buf) - 1)
		n = sizeof(buf) - 1;
	memcpy(buf, s, n);
	buf[n] = 0;
	if (n > 1)
		buf[n - 1] = '~';
	return text(c, x, y, size, col, buf);
}
