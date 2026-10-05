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
 * Tab, the arrows and two fingers across the pad move the selection one
 * icon to the right or the left, around at the ends; the caller then
 * brings the selected application, or gives the switcher up.
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

/* The fingers' travel across the pad of one step (micrometres). */
#define ZWL_SWITCHER_STEP_UM		12000

/* The switcher: whether it is on, how it was opened, where it shows, the applications' keys (the bar's order), the selection, and the pad's travel not yet a step. */
struct zwl_switcher {
	unsigned on;
	unsigned via;
	unsigned placement;
	char keys[ZWL_APPS_MAX][ZWL_APPS_KEY];
	unsigned count;
	unsigned index;
	int32_t travel_um;
};

int zwl_switcher_open(struct zwl_switcher *switcher, const struct zwl_apps *apps, int current, unsigned via, unsigned placement);
void zwl_switcher_step(struct zwl_switcher *switcher, int delta);
int zwl_switcher_travel(struct zwl_switcher *switcher, int32_t dx_um);
const char *zwl_switcher_selected(const struct zwl_switcher *switcher);
void zwl_switcher_close(struct zwl_switcher *switcher);

#endif
