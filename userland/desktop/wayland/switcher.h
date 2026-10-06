/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The application switcher's state (switcher.c, ws142-p005): opened by
 * Alt+Tab or a tap of three fingers on the touch pad (D1), it takes the
 * desktop's applications in the order of the bar's icons, from the left,
 * and selects the current application (BUG-209, the 2026-10-05 user
 * decision: the order a person sees, never the compositor's order of use).
 * Tab, the arrows and a swipe of two fingers across the pad (one swipe a
 * step, swipe.c, ws142-p009) move the selection one icon to the right or
 * the left, around at the ends; the caller then
 * brings the selected application, or gives the switcher up.  A quick
 * Alt+Tab (Alt let go within ZWL_SWITCHER_QUICK_MS of the opening, before
 * any step) leaves it open (BUG-209, the 2026-10-06 user instruction):
 * from then on letting Alt go brings nothing, and only Enter, a click or
 * a tap brings, Esc or a click elsewhere gives up.
 *
 * It knows nothing of the server: the caller hands it the applications
 * (apps.c), the steps and the pad's travel, and draws and acts on what it
 * selects (apps-bar.c).  So the host tests run it alone.
 */

#ifndef ZWL_SWITCHER_H
#define ZWL_SWITCHER_H

#include "apps.h"

#include <stdint.h>

/* How the switcher was opened: from the keyboard (Alt+Tab) or the touch pad (a tap of three fingers). */
#define ZWL_SWITCHER_VIA_KEYS		1U
#define ZWL_SWITCHER_VIA_PAD		2U

/* Where it shows: at the application's icon in the bar, or in the middle of the output (a docked window has the bar). */
#define ZWL_SWITCHER_BAR		0U
#define ZWL_SWITCHER_CENTER		1U

/* How soon after the opening Alt let go leaves the switcher open, when no step came (milliseconds). */
#define ZWL_SWITCHER_QUICK_MS		500U

/*
 * The switcher: whether it is on, how it was opened, where it shows, the
 * applications' keys (the bar's order), and the selection.  When it opened (milliseconds, the caller's
 * clock), the steps taken since, and whether a quick Alt+Tab left it open
 * (sticky: letting Alt go no longer brings the selection).
 */
struct zwl_switcher {
	unsigned on;
	unsigned via;
	unsigned placement;
	char keys[ZWL_APPS_MAX][ZWL_APPS_KEY];
	unsigned count;
	unsigned index;
	uint64_t opened_ms;
	unsigned steps;
	unsigned sticky;
};

int zwl_switcher_open(struct zwl_switcher *switcher, const struct zwl_apps *apps, int current, unsigned via, unsigned placement);
void zwl_switcher_step(struct zwl_switcher *switcher, int delta);
const char *zwl_switcher_selected(const struct zwl_switcher *switcher);
int zwl_switcher_alt_released(struct zwl_switcher *switcher, uint64_t now_ms);
void zwl_switcher_close(struct zwl_switcher *switcher);

#endif
