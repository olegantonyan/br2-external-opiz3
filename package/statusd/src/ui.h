/* SPDX-License-Identifier: GPL-2.0 */
#ifndef UI_H
#define UI_H

#include "draw.h"
#include "stats.h"

struct uiinfo {
	int w, h, hz, keyboards;
	const char *monitor;
	unsigned hotplugs, modesets, failures;
	double frame_ms;
};

void ui_reset(void);
void ui_draw(struct canvas *c, const struct stats *s, const struct uiinfo *ui);

#endif
