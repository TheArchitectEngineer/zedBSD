/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The cursor's box (cursor-box.c, ws213-p004; the design is
 * plan/ws213/phase001/phase.md 6.1 and 6.2).
 *
 * The box holds the pointer's place as the input thread last wrote it:
 * the input thread adds each motion to it, and the main thread's warps
 * (the pointer carried off an output that goes, an absolute device's
 * place) replace it.  It also keeps the outputs' rectangles the motions
 * are computed over, so that a place and the rectangles it was computed
 * with are always written in one section.  The main thread's own view of
 * the pointer (server->pointer_x) follows the box through the input
 * thread's events and may be behind it.
 *
 * One mutex (the cursor lock of the design) guards it; its holder calls
 * nothing that waits, prints or enters Vulkan (design 4.6).
 */

#ifndef KWL_CURSOR_BOX_H
#define KWL_CURSOR_BOX_H

#include "plane.h"

#include <pthread.h>
#include <stdint.h>

/*
 * The pointer's place after one write of the box, as the writer reports it
 * to the main thread: whether the place changed, whether it crossed to
 * another output, the place, and the warp count it was computed under (a
 * motion computed before a later warp is not taken as the pointer's
 * place).  event_us is the evdev time of the report that moved it and
 * write_us when the box was written (kwl_microseconds), the two ends of
 * the box's lag (design 6.4).
 */
struct kwl_cursor_motion {
	unsigned moved;
	unsigned crossed;
	int32_t x;
	int32_t y;
	unsigned output;
	uint64_t warp;
	uint64_t event_us;
	uint64_t write_us;
};

/*
 * One warp of the main thread: given the box's place and output, gives the
 * new ones.  It runs under the cursor lock, so it only computes (plane.c's
 * functions): no output, no allocation.
 */
typedef void kwl_cursor_box_move_fn(const void *context, const struct kwl_plane_rect *outputs, int32_t *x, int32_t *y, unsigned *output);

/*
 * The box shared by the input thread (motions), the main thread (warps,
 * the outputs' rectangles) and the cursor thread (reads).  writes counts
 * every write, so a reader tells a new place from one it has seen; warp
 * counts the main thread's warps only.
 */
struct kwl_cursor_box {
	pthread_mutex_t lock;
	pthread_cond_t changed;
	int32_t x;
	int32_t y;
	unsigned output;
	uint64_t event_us;
	uint64_t write_us;
	uint64_t warp;
	uint64_t writes;
	struct kwl_plane_rect outputs[KWL_PLANE_SLOTS];
	unsigned output_count;
};

int kwl_cursor_box_init(struct kwl_cursor_box *box, int32_t x, int32_t y, uint32_t width, uint32_t height);
void kwl_cursor_box_destroy(struct kwl_cursor_box *box);
void kwl_cursor_box_move(struct kwl_cursor_box *box, int64_t dx, int64_t dy, uint64_t event_us, struct kwl_cursor_motion *motion);
void kwl_cursor_box_place(struct kwl_cursor_box *box, int32_t value_x, int32_t minimum_x, int32_t maximum_x, int32_t value_y, int32_t minimum_y, int32_t maximum_y, int64_t dx, int64_t dy, uint64_t event_us, struct kwl_cursor_motion *motion);
void kwl_cursor_box_warp(struct kwl_cursor_box *box, const struct kwl_plane_rect *outputs, unsigned count, kwl_cursor_box_move_fn *move, const void *context, struct kwl_cursor_motion *motion);
void kwl_cursor_box_outputs(struct kwl_cursor_box *box, const struct kwl_plane_rect *outputs, unsigned count);
void kwl_cursor_box_read(struct kwl_cursor_box *box, int32_t *x, int32_t *y, unsigned *output);
int32_t kwl_cursor_scale_absolute(int32_t value, int32_t minimum, int32_t maximum, uint32_t size);
int32_t kwl_cursor_clamp(int64_t position, uint32_t size);

#endif
