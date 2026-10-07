/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws181-p005: prints the slots of each layout's small drawing in the
 * arrangement menu, as arrange-shell.c's arrange_draw_icon computes them
 * (three windows, four in the grid, in an area five times the 60 x 40
 * drawing with the margin around it; ws181-p006), one line a slot: "layout x y w h",
 * in the drawing's pixels.
 */

#include "arrange.h"

#include <stdio.h>

/* The drawing's size and how much larger the area its slots are computed in is (arrange-shell.c). */
#define ICON_WIDTH	60
#define ICON_HEIGHT	40
#define ICON_SCALE	5

/*
 * Prints every layout's slots.
 */
int
main(void)
{
	struct kwl_arrange_rect area;
	struct kwl_arrange_rect slots[KWL_ARRANGE_MAX];
	unsigned layout;
	unsigned count;
	unsigned made;
	unsigned index;

	/* Each layout, as the menu draws it. */
	for (layout = 0U; layout < KWL_ARRANGE_LAYOUTS; layout++) {
		/* Three windows, four in the grid, in an area five times the drawing's size. */
		count = 3U;
		if (layout == KWL_ARRANGE_GRID)
			count = 4U;
		area.x = -KWL_ARRANGE_MARGIN;
		area.y = -KWL_ARRANGE_MARGIN;
		area.width = ICON_WIDTH * ICON_SCALE + 2 * KWL_ARRANGE_MARGIN;
		area.height = ICON_HEIGHT * ICON_SCALE + 2 * KWL_ARRANGE_MARGIN;
		made = kwl_arrange_slots(layout, count, &area, slots);

		/* Each slot scaled down, as the drawing has it. */
		for (index = 0U; index < made; index++) {
			printf("%u %g %g %g %g\n",
			       layout,
			       (double)slots[index].x / ICON_SCALE,
			       (double)slots[index].y / ICON_SCALE,
			       (double)slots[index].width / ICON_SCALE - 2.0,
			       (double)slots[index].height / ICON_SCALE - 2.0);
		}
	}

	/* Succeeded: every layout is printed. */
	return 0;
}
