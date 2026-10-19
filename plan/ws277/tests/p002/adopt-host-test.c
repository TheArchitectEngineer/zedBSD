/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the DC8200 takeover decision: a controller as the loader
 * leaves it is taken over, and each condition that fails is named.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/dc8200-adopt.h"

static int failures;
static int passes;

static void check(int condition, const char *what);
static void lit_state(struct powervr_dc_state *state, struct powervr_dc_screen *screen);
static void expect(const struct powervr_dc_state *state, const struct powervr_dc_screen *screen, enum powervr_dc_verdict wanted, unsigned wanted_plane, const char *what);

int
main(
	void)
{
	struct powervr_dc_state state;
	struct powervr_dc_screen screen;

	/* The loader's screen on plane 0, panel 0. */
	lit_state(&state, &screen);
	expect(&state, &screen, POWERVR_DC_GO, 0, "plane 0 on panel 0 is taken over");

	/* The same screen on plane 1, plane 0 off. */
	lit_state(&state, &screen);
	state.planes[1] = state.planes[0];
	state.planes[0].extra = 0;
	expect(&state, &screen, POWERVR_DC_GO, 1, "plane 1 is chosen when plane 0 is off");

	/* Plane 0 lit on panel 1 only. */
	lit_state(&state, &screen);
	state.planes[0].extra |= 1U << POWERVR_DC_EXTRA_PANEL_SHIFT;
	expect(&state, &screen, POWERVR_DC_NO_PLANE, POWERVR_DC_PLANES, "a plane on the other panel is not chosen");

	/* Another model. */
	lit_state(&state, &screen);
	state.model = 0x8000U;
	expect(&state, &screen, POWERVR_DC_UNKNOWN_MODEL, POWERVR_DC_PLANES, "another model is refused");

	/* Each mismatch of the plane against the framebuffer. */
	lit_state(&state, &screen);
	state.planes[0].address += 0x1000U;
	expect(&state, &screen, POWERVR_DC_OTHER_ADDRESS, 0, "another address is refused");

	lit_state(&state, &screen);
	screen.physical = 0x100000000ULL + state.planes[0].address;
	expect(&state, &screen, POWERVR_DC_OTHER_ADDRESS, 0, "a framebuffer above 4 GiB is refused");

	lit_state(&state, &screen);
	state.planes[0].stride = 7680U + 64U;
	expect(&state, &screen, POWERVR_DC_OTHER_STRIDE, 0, "another stride is refused");

	lit_state(&state, &screen);
	state.planes[0].size = 1280U | (720U << POWERVR_DC_SIZE_HEIGHT_SHIFT);
	expect(&state, &screen, POWERVR_DC_OTHER_SIZE, 0, "another size is refused");

	lit_state(&state, &screen);
	state.planes[0].size = 1920U | (1079U << POWERVR_DC_SIZE_HEIGHT_SHIFT);
	expect(&state, &screen, POWERVR_DC_OTHER_SIZE, 0, "another height is refused");

	lit_state(&state, &screen);
	state.planes[0].config |= 4U << POWERVR_DC_CONFIG_TILE_SHIFT;
	expect(&state, &screen, POWERVR_DC_TILED, 0, "a tiled plane is refused");

	lit_state(&state, &screen);
	state.planes[0].config |= 1U << POWERVR_DC_CONFIG_ROTATE_SHIFT;
	expect(&state, &screen, POWERVR_DC_ROTATED, 0, "a rotated plane is refused");

	lit_state(&state, &screen);
	state.planes[0].config |= POWERVR_DC_CONFIG_SCALE;
	expect(&state, &screen, POWERVR_DC_SCALED, 0, "a scaled plane is refused");

	lit_state(&state, &screen);
	state.planes[0].extra |= POWERVR_DC_EXTRA_COMPRESSED;
	expect(&state, &screen, POWERVR_DC_COMPRESSED, 0, "a compressed plane is refused");

	/* The commit bit and other format bits do not change the decision. */
	lit_state(&state, &screen);
	state.planes[0].extra |= POWERVR_DC_EXTRA_COMMIT;
	state.planes[0].config |= 0x3fU << 26;
	expect(&state, &screen, POWERVR_DC_GO, 0, "commit and format bits are ignored");

	/* Every verdict has a name. */
	check(strcmp(powervr_dc_verdict_name(POWERVR_DC_GO), "takeover possible") == 0, "go is named");
	check(strcmp(powervr_dc_verdict_name(POWERVR_DC_COMPRESSED), "plane is compressed") == 0, "compressed is named");

	printf("%s adopt-host-test (%d passes, %d failures)\n", failures == 0 ? "PASS" : "FAIL", passes, failures);
	return failures == 0 ? 0 : 1;
}

/* Counts one check and names a failed one. */
static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: %s\n", what);
}

/* Fills a controller showing a 1920x1080 framebuffer on plane 0, panel 0. */
static void
lit_state(
	struct powervr_dc_state *state,
	struct powervr_dc_screen *screen)
{
	memset(state, 0, sizeof(*state));
	state->model = POWERVR_DC_MODEL;
	state->revision = 0x5721U;
	state->customer = 0x310U;
	state->planes[0].address = 0xfe000000U;
	state->planes[0].stride = 7680U;
	state->planes[0].config = 0x4U << 26;
	state->planes[0].extra = POWERVR_DC_EXTRA_ENABLED;
	state->planes[0].size = 1920U | (1080U << POWERVR_DC_SIZE_HEIGHT_SHIFT);

	screen->physical = 0xfe000000ULL;
	screen->pitch = 7680U;
	screen->width = 1920U;
	screen->height = 1080U;
}

/* Checks the verdict and the chosen plane of one case. */
static void
expect(
	const struct powervr_dc_state *state,
	const struct powervr_dc_screen *screen,
	enum powervr_dc_verdict wanted,
	unsigned wanted_plane,
	const char *what)
{
	enum powervr_dc_verdict verdict;
	unsigned plane;

	verdict = powervr_dc_judge(state, screen, 0, &plane);
	check(verdict == wanted && plane == wanted_plane, what);
}
