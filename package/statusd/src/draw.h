/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

struct canvas {
	uint32_t *px;
	int w, h;
};

void draw_reset(void);
void fill(struct canvas *c, int x, int y, int w, int h, uint32_t col);
void blend(struct canvas *c, int x, int y, int w, int h, uint32_t col, int alpha);
void vgradient(struct canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void round_rect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t col);
void dot(struct canvas *c, int cx, int cy, int r, uint32_t col);
void bar(struct canvas *c, int x, int y, int w, int h, double frac, uint32_t fg, uint32_t bg);
void spark(struct canvas *c, int x, int y, int w, int h, const float *v, int n, int head, float max, uint32_t col);
int text_width(int size, const char *s);
int text(struct canvas *c, int x, int y, int size, uint32_t col, const char *s);
int text_right(struct canvas *c, int right, int y, int size, uint32_t col, const char *s);
int text_fit(struct canvas *c, int x, int y, int maxw, int size, uint32_t col, const char *s);

#endif
