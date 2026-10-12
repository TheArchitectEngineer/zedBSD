/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of a native pass narrowed to tile boxes (ws211-p009,
 * src/drivers/gpu/bcm2711/native-pass.c): the render list names only the
 * supertiles a box meets, in row order, its length is the one measured,
 * a pass without boxes names every supertile of its rectangle, and boxes
 * that are too many, inverted or outside the rectangle are refused.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/native-pass.h"

/* The synthetic list images. */
static struct bcm2711_native_pass_image images[3];

/* Makes a pass over a whole framebuffer of a size, loading its output. */
static void
pass_init(
	struct bcm2711_native_pass *state,
	uint32_t width,
	uint32_t height)
{
	memset(state, 0, sizeof(*state));
	state->width = width;
	state->height = height;
	state->pitch = (width * 4U + 63U) & ~63U;
	state->output = 0x4000000U;
	state->output_bytes = state->pitch * height;
	state->pool = 0x8000000U;
	state->pool_bytes = 0x200000U;
	state->state = 0xa000000U;
	state->state_bytes = 0x40000U;
	state->last_x = (width + 63U) / 64U - 1U;
	state->last_y = (height + 63U) / 64U - 1U;
	state->load = 1;
	state->store = 1;
}

/* Adds a tile box. */
static void
box_add(
	struct bcm2711_native_pass *state,
	uint32_t first_x,
	uint32_t first_y,
	uint32_t last_x,
	uint32_t last_y)
{
	state->boxes[state->box_count][0] = first_x;
	state->boxes[state->box_count][1] = first_y;
	state->boxes[state->box_count][2] = last_x;
	state->boxes[state->box_count][3] = last_y;
	state->box_count++;
}

/*
 * Encodes a pass and checks its render list: the supertile entries
 * (packet 23, x, y) from byte 102, as many as expected, then the end
 * marker, at the measured length.  Returns the entries in out.
 */
static void
encode_expect(
	const struct bcm2711_native_pass *state,
	uint32_t expected,
	uint32_t (*out)[2])
{
	struct bcm2711_native_pass_sizes sizes;
	uint32_t index;
	int error;

	error = bcm2711_native_pass_measure(state, 1, &sizes);
	assert(error == 0);
	assert(sizes.render == 103U + expected * 3U);
	error = bcm2711_native_pass_encode(state, 1, &images[0], &images[1], &images[2]);
	assert(error == 0);
	assert(images[1].used == sizes.render);
	for (index = 0; index < expected; index++) {
		assert(images[1].bytes[102U + index * 3U] == 23);
		out[index][0] = images[1].bytes[102U + index * 3U + 1U];
		out[index][1] = images[1].bytes[102U + index * 3U + 2U];
	}
	assert(images[1].bytes[102U + expected * 3U] == 13);
}

int
main(void)
{
	static uint32_t entries[300][2];
	struct bcm2711_native_pass state;
	struct bcm2711_native_pass_sizes sizes;
	uint32_t index;
	int error;

	/* Independent CPU storage for the three lists. */
	for (index = 0; index < 3U; index++) {
		images[index].capacity = 65536U;
		images[index].address = (index + 1U) * 0x100000U;
		images[index].bytes = malloc(images[index].capacity);
		assert(images[index].bytes != NULL);
		memset(images[index].bytes, 0, images[index].capacity);
	}

	/* 800x480: 13x8 tiles, groups of one; no box renders all 104. */
	pass_init(&state, 800, 480);
	encode_expect(&state, 104, entries);
	error = bcm2711_native_pass_measure(&state, 1, &sizes);
	assert(error == 0 && sizes.supertile_width == 1U);

	/* The clock (tiles 11..12 of row 0) and a hovered cell (tiles 4..5, rows 3..4): 2 + 4 tiles. */
	box_add(&state, 11, 0, 12, 0);
	box_add(&state, 4, 3, 5, 4);
	encode_expect(&state, 6, entries);
	assert(entries[0][0] == 11 && entries[0][1] == 0);
	assert(entries[1][0] == 12 && entries[1][1] == 0);
	assert(entries[2][0] == 4 && entries[2][1] == 3);
	assert(entries[5][0] == 5 && entries[5][1] == 4);
	assert(bcm2711_native_pass_tiles(&state) == 6U);

	/* A box inside another, and two sharing tiles, name each tile once. */
	box_add(&state, 4, 3, 4, 3);
	box_add(&state, 5, 4, 6, 4);
	encode_expect(&state, 7, entries);

	/* A rectangle narrower than the framebuffer: boxes inside it. */
	pass_init(&state, 800, 480);
	state.first_x = 2;
	state.last_x = 9;
	state.first_y = 1;
	state.last_y = 6;
	encode_expect(&state, 48, entries);
	box_add(&state, 2, 1, 2, 1);
	box_add(&state, 9, 6, 9, 6);
	encode_expect(&state, 2, entries);
	assert(entries[0][0] == 2 && entries[0][1] == 1 && entries[1][0] == 9 && entries[1][1] == 6);

	/* A box outside the rectangle, inverted, or one too many is refused. */
	box_add(&state, 10, 1, 10, 1);
	error = bcm2711_native_pass_measure(&state, 1, &sizes);
	assert(error == EINVAL);
	state.box_count--;
	box_add(&state, 5, 3, 4, 3);
	error = bcm2711_native_pass_measure(&state, 1, &sizes);
	assert(error == EINVAL);
	state.box_count = BCM2711_NATIVE_PASS_BOXES + 1U;
	error = bcm2711_native_pass_measure(&state, 1, &sizes);
	assert(error == EINVAL);

	/* 1920x1080: 30x17 tiles in groups of two (15x9 = 135); a box of one tile renders its group. */
	pass_init(&state, 1920, 1080);
	encode_expect(&state, 135, entries);
	error = bcm2711_native_pass_measure(&state, 1, &sizes);
	assert(error == 0 && sizes.supertile_width == 2U);
	box_add(&state, 29, 0, 29, 0);
	box_add(&state, 13, 7, 14, 8);
	encode_expect(&state, 1 + 4, entries);
	assert(entries[0][0] == 14 && entries[0][1] == 0);
	assert(entries[1][0] == 6 && entries[1][1] == 3);
	assert(entries[4][0] == 7 && entries[4][1] == 4);
	assert(bcm2711_native_pass_tiles(&state) == 4U + 16U);
	state.box_count = 0;
	assert(bcm2711_native_pass_tiles(&state) == 510U);
	box_add(&state, 29, 0, 29, 0);
	box_add(&state, 13, 7, 14, 8);

	/* A copy pass narrowed the same way (its tiles are loaded from the source). */
	state.source = 0x6000000U;
	state.source_pitch = state.pitch;
	state.source_bytes = state.output_bytes;
	encode_expect(&state, 5, entries);

	printf("native-pass-boxes-host-test: PASS\n");
	return 0;
}
