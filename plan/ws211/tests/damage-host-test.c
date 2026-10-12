/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of a window commit's damage (ws211-p008,
 * userland/desktop/wayland/damage.c): a toplevel's new image of the same
 * size marks only the part of its body the client damaged, moved to the
 * body's place and kept inside it; a commit without damage, damage wholly
 * outside the image, or an image drawn at another size marks the whole
 * body; a new size marks the whole output.
 *
 * The damage's rectangles (ws211-p009): overlapping ones are joined,
 * nearly touching ones too, far ones stay apart, a full list joins the
 * pair that grows least, and the list never holds two that overlap.
 *
 * The cut of a quad around opaque shapes drawn over it (ws211-p011): pixel
 * by pixel, no pixel is drawn twice, every pixel of the quad is drawn
 * unless an opaque rounded rectangle fully covers it (cover 1, as
 * panel.frag works it out), and the pieces stay few.
 */

#include "userland/desktop/wayland/compose.h"
#include "userland/desktop/wayland/glass.h"
#include "userland/desktop/wayland/notify-view.h"
#include "userland/desktop/wayland/popup.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The images' sizes kwl_buffer_size answers, and the body shell.c answers. */
static uint32_t old_width;
static uint32_t old_height;
static uint32_t new_width;
static uint32_t new_height;
static int32_t body[4];
static struct kwl_object old_image;
static struct kwl_object new_image;

/* Answers an image's size: the old or the new one, as the test sets them. */
void
kwl_buffer_size(
	const struct kwl_object *buffer,
	uint32_t *width,
	uint32_t *height)
{
	if (buffer == &old_image) {
		*width = old_width;
		*height = old_height;
		return;
	}
	*width = new_width;
	*height = new_height;
}

/* Answers the body's rectangle, as the test sets it; nothing near is glass. */
int
kwl_glass_body_damage(
	struct kwl_server *server,
	struct kwl_object *surface,
	int32_t *rect)
{
	(void)server;
	(void)surface;
	memcpy(rect, body, sizeof(body));
	return 1;
}

/* The cursor is not on its plane. */
int
kwl_cursor_plane_takes(
	struct kwl_server *server)
{
	(void)server;
	return 0;
}

/* The pointer's helpers, not reached by a commit. */
struct kwl_object *
kwl_glass_body_at(
	struct kwl_server *server,
	int32_t x,
	int32_t y)
{
	(void)server;
	(void)x;
	(void)y;
	return NULL;
}

/* The look is still. */
int
kwl_glass_hover_still(
	struct kwl_server *server)
{
	(void)server;
	return 1;
}

/* The pointer is calm. */
int
kwl_glass_pointer_calm(
	struct kwl_server *server,
	int32_t x,
	int32_t y)
{
	(void)server;
	(void)x;
	(void)y;
	return 1;
}

/* No clock is read. */
uint64_t
kwl_microseconds(void)
{
	return 0U;
}

/* No dialog or panel is showing. */
int
kwl_notify_log_showing(void)
{
	return 0;
}

/* No power dialog. */
int
kwl_power_dialog_showing(
	struct kwl_server *server)
{
	(void)server;
	return 0;
}

/* No arrangement. */
int
kwl_arrange_showing(
	struct kwl_server *server)
{
	(void)server;
	return 0;
}

/* No Bluetooth question. */
int
kwl_bluetooth_ask_showing(void)
{
	return 0;
}

/* Commits a new image of the test's sizes with the given damage (or none) and returns the server's state. */
static void
commit(
	struct kwl_server *server,
	struct kwl_object *surface,
	const int32_t *damage)
{
	server->dirty = 0;
	server->damaged = 0;
	server->damage_count = 0;
	memset(server->damage, 0, sizeof(server->damage));
	surface->committed_damaged = 0;
	if (damage != NULL) {
		memcpy(surface->committed_damage, damage, sizeof(surface->committed_damage));
		surface->committed_damaged = 1;
	}
	kwl_damage_commit(server, surface, &old_image);
}

/* Checks that the frame draws exactly one rectangle (left, top, right, bottom). */
static void
expect(
	const struct kwl_server *server,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom)
{
	assert(!server->dirty);
	assert(server->damaged);
	assert(server->damage_count == 1U);
	if (server->damage[0][0] != left ||
	    server->damage[0][1] != top ||
	    server->damage[0][2] != right ||
	    server->damage[0][3] != bottom) {
		printf("damage %d,%d,%d,%d expected %d,%d,%d,%d\n", server->damage[0][0], server->damage[0][1], server->damage[0][2], server->damage[0][3], left, top,
		       right, bottom);
		assert(0);
	}
}

/* Adds a rectangle to a list. */
static void
add(
	int32_t (*rects)[4],
	unsigned *count,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom)
{
	int32_t rect[4];

	rect[0] = left;
	rect[1] = top;
	rect[2] = right;
	rect[3] = bottom;
	kwl_damage_rects_add(rects, count, rect);
}

/* Tells whether a list holds a rectangle exactly. */
static int
holds(
	int32_t (*rects)[4],
	unsigned count,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom)
{
	unsigned index;

	for (index = 0; index < count; index++) {
		if (rects[index][0] == left && rects[index][1] == top && rects[index][2] == right && rects[index][3] == bottom)
			return 1;
	}
	return 0;
}

/* Checks that no two rectangles of a list overlap, and that each contains every rectangle given (inside). */
static void
check_list(
	int32_t (*rects)[4],
	unsigned count,
	int32_t (*given)[4],
	unsigned given_count)
{
	unsigned first;
	unsigned second;
	int inside;

	assert(count <= KWL_DAMAGE_RECTS);
	for (first = 0; first < count; first++) {
		assert(rects[first][2] > rects[first][0] && rects[first][3] > rects[first][1]);
		for (second = first + 1U; second < count; second++) {
			if (rects[first][0] < rects[second][2] && rects[second][0] < rects[first][2] && rects[first][1] < rects[second][3] &&
			    rects[second][1] < rects[first][3]) {
				printf("overlap %d,%d,%d,%d and %d,%d,%d,%d\n", rects[first][0], rects[first][1], rects[first][2], rects[first][3], rects[second][0],
				       rects[second][1], rects[second][2], rects[second][3]);
				assert(0);
			}
		}
	}
	for (second = 0; second < given_count; second++) {
		inside = 0;
		for (first = 0; first < count; first++) {
			if (given[second][0] >= rects[first][0] && given[second][1] >= rects[first][1] && given[second][2] <= rects[first][2] &&
			    given[second][3] <= rects[first][3])
				inside = 1;
		}
		assert(inside);
	}
}

/* The rectangle list's rules (ws211-p009 D1). */
static void
test_rects(void)
{
	int32_t rects[KWL_DAMAGE_RECTS][4];
	int32_t given[64][4];
	unsigned count;
	unsigned given_count;
	unsigned round;
	unsigned seed;
	int32_t x;
	int32_t y;

	/* Far apart (the clock and a hovered cell): two. */
	count = 0;
	add(rects, &count, 700, 0, 790, 30);
	add(rects, &count, 300, 200, 404, 309);
	assert(count == 2);
	assert(holds(rects, count, 700, 0, 790, 30));
	assert(holds(rects, count, 300, 200, 404, 309));

	/* Overlapping: one box. */
	add(rects, &count, 350, 250, 450, 350);
	assert(count == 2);
	assert(holds(rects, count, 300, 200, 450, 350));

	/* Next to each other (the hovered cell moving one cell): one box, it adds no area. */
	count = 0;
	add(rects, &count, 0, 0, 104, 109);
	add(rects, &count, 104, 0, 208, 109);
	assert(count == 1);
	assert(holds(rects, count, 0, 0, 208, 109));

	/* A small gap (box adds 104x30 = 3120 < 4096): one. */
	count = 0;
	add(rects, &count, 0, 0, 104, 100);
	add(rects, &count, 0, 130, 104, 230);
	assert(count == 1);

	/* A larger gap (104x50 = 5200 > 4096): two. */
	count = 0;
	add(rects, &count, 0, 0, 104, 100);
	add(rects, &count, 0, 150, 104, 250);
	assert(count == 2);

	/* An empty rectangle adds nothing. */
	add(rects, &count, 10, 10, 10, 20);
	assert(count == 2);

	/* A joined box that meets a third joins it too (no overlap left). */
	count = 0;
	add(rects, &count, 0, 0, 10, 10);
	add(rects, &count, 500, 0, 510, 10);
	add(rects, &count, 200, 0, 300, 400);
	add(rects, &count, 5, 300, 505, 310);
	check_list(rects, count, NULL, 0);
	assert(count == 1);
	assert(holds(rects, count, 0, 0, 510, 400));

	/* Five far apart: four, the closest pair joined. */
	count = 0;
	add(rects, &count, 0, 0, 10, 10);
	add(rects, &count, 1000, 0, 1010, 10);
	add(rects, &count, 0, 1000, 10, 1010);
	add(rects, &count, 1000, 1000, 1010, 1010);
	add(rects, &count, 1000, 200, 1010, 210);
	assert(count == 4);
	assert(holds(rects, count, 1000, 0, 1010, 210));

	/* Edges far outside are kept near the output. */
	count = 0;
	add(rects, &count, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
	assert(count == 1);
	assert(rects[0][0] == -(1 << 20) && rects[0][2] == (1 << 20));

	/* Many random ones: never more than the room, never an overlap, each one inside the list. */
	seed = 12345U;
	for (round = 0; round < 2000; round++) {
		count = 0;
		given_count = 1U + round % 60U;
		for (x = 0; x < (int32_t)given_count; x++) {
			seed = seed * 1103515245U + 12345U;
			y = (int32_t)((seed >> 8) % 1900U);
			seed = seed * 1103515245U + 12345U;
			given[x][0] = y;
			given[x][1] = (int32_t)((seed >> 8) % 1100U);
			seed = seed * 1103515245U + 12345U;
			given[x][2] = y + 1 + (int32_t)((seed >> 8) % 300U);
			seed = seed * 1103515245U + 12345U;
			given[x][3] = given[x][1] + 1 + (int32_t)((seed >> 8) % 200U);
			kwl_damage_rects_add(rects, &count, given[x]);
		}
		check_list(rects, count, given, given_count);
	}
}

/* Signed distance from a point to a rounded rectangle (x, y, width, height), negative inside, as panel.frag's rounded(). */
static float
cut_rounded(
	float x,
	float y,
	const float *box,
	float radius)
{
	float qx;
	float qy;
	float outside_x;
	float outside_y;
	float inside;

	qx = fabsf(x - (box[0] + box[2] * 0.5f)) - box[2] * 0.5f + radius;
	qy = fabsf(y - (box[1] + box[3] * 0.5f)) - box[3] * 0.5f + radius;
	outside_x = qx > 0.0f ? qx : 0.0f;
	outside_y = qy > 0.0f ? qy : 0.0f;
	inside = qx > qy ? qx : qy;
	if (inside > 0.0f)
		inside = 0.0f;
	return sqrtf(outside_x * outside_x + outside_y * outside_y) + inside - radius;
}

/*
 * Cuts a quad around opaque rounded rectangles (boxes with one radius) as
 * panels.c and shell.c do (each box less its radius and one pixel), then
 * checks every pixel centre: drawn at most once, and left out only under a
 * box that covers it fully.  Returns the pieces.
 */
static unsigned
cut_check(
	const float *quad,
	const float (*boxes)[4],
	unsigned box_count,
	float radius)
{
	float holes[8][4];
	float pieces[KWL_CUT_PIECES][4];
	float cx;
	float cy;
	float distance;
	unsigned count;
	unsigned index;
	unsigned covering;
	unsigned box;
	int x;
	int y;
	int hidden;

	for (index = 0U; index < box_count; index++) {
		holes[index][0] = boxes[index][0] + radius + 1.0f;
		holes[index][1] = boxes[index][1] + radius + 1.0f;
		holes[index][2] = boxes[index][2] - 2.0f * (radius + 1.0f);
		holes[index][3] = boxes[index][3] - 2.0f * (radius + 1.0f);
	}
	count = kwl_damage_rects_cut(quad, (const float (*)[4])holes, box_count, pieces);
	assert(count <= KWL_CUT_PIECES);
	for (y = (int)floorf(quad[1]) - 2; y < (int)ceilf(quad[1] + quad[3]) + 2; y++) {
		for (x = (int)floorf(quad[0]) - 2; x < (int)ceilf(quad[0] + quad[2]) + 2; x++) {
			cx = (float)x + 0.5f;
			cy = (float)y + 0.5f;
			covering = 0U;
			for (index = 0U; index < count; index++) {
				if (cx >= pieces[index][0] && cx < pieces[index][0] + pieces[index][2] &&
				    cy >= pieces[index][1] && cy < pieces[index][1] + pieces[index][3])
					covering++;
			}
			assert(covering <= 1U);

			/* Outside the quad nothing is drawn; inside, only a fully covered pixel may be left out. */
			if (cx < quad[0] || cx >= quad[0] + quad[2] || cy < quad[1] || cy >= quad[1] + quad[3]) {
				assert(covering == 0U);
				continue;
			}
			if (covering == 1U)
				continue;
			hidden = 0;
			for (box = 0U; box < box_count; box++) {
				distance = cut_rounded(cx, cy, boxes[box], radius);
				if (0.5f - distance >= 1.0f)
					hidden = 1;
			}
			if (!hidden)
				printf("pixel %d,%d left out but not covered\n", x, y);
			assert(hidden);
		}
	}
	return count;
}

/* The cut of shadows and the glass between cards around the opaque glass over them (ws211-p011). */
static void
test_cut(void)
{
	static const float window[1][4] = { { 100.0f, 80.0f, 776.0f, 360.0f } };
	static const float small[1][4] = { { 300.0f, 200.0f, 90.0f, 60.0f } };
	static const float covering[1][4] = { { 0.0f, 0.0f, 400.0f, 300.0f } };
	static const float cards[8][4] = {
		{ 20.0f, 20.0f, 240.0f, 180.0f }, { 280.0f, 20.0f, 240.0f, 180.0f },
		{ 540.0f, 20.0f, 240.0f, 180.0f }, { 20.0f, 220.0f, 240.0f, 180.0f },
		{ 280.0f, 220.0f, 240.0f, 180.0f }, { 540.0f, 220.0f, 240.0f, 180.0f },
		{ 20.0f, 420.0f, 760.0f, 120.0f }, { 20.0f, 560.0f, 120.0f, 40.0f }
	};
	float shadow[4];
	float gaps[4] = { 20.0f, 20.0f, 760.0f, 580.0f };
	float card_shadow[4];
	unsigned count;

	/* A window's shadow (22 soft, 8 down) around its opaque body (radius 14): four bands. */
	shadow[0] = 100.0f - 44.0f;
	shadow[1] = 88.0f - 44.0f;
	shadow[2] = 776.0f + 88.0f;
	shadow[3] = 360.0f + 88.0f;
	count = cut_check(shadow, window, 1U, 14.0f);
	assert(count == 4U);

	/* No hole: the quad itself. */
	count = cut_check(shadow, window, 0U, 14.0f);
	assert(count == 1U);

	/* A hole too small to be worth its draws is not cut. */
	shadow[0] = 250.0f;
	shadow[1] = 150.0f;
	shadow[2] = 200.0f;
	shadow[3] = 160.0f;
	count = cut_check(shadow, small, 1U, 14.0f);
	assert(count == 1U);

	/* A quad with fractional edges (a layer scaled) inside a hole: nothing is left. */
	shadow[0] = 50.25f;
	shadow[1] = 60.75f;
	shadow[2] = 150.5f;
	shadow[3] = 120.125f;
	count = cut_check(shadow, covering, 1U, 14.0f);
	assert(count == 0U);

	/* A hole across the quad's edge, fractional edges: the quad less it. */
	shadow[0] = 330.25f;
	shadow[1] = 10.5f;
	shadow[2] = 200.0f;
	shadow[3] = 400.0f;
	count = cut_check(shadow, covering, 1U, 14.0f);
	assert(count >= 1U);

	/* The glass between eight cards: cut while the pieces stay at most eight. */
	count = cut_check(gaps, cards, 8U, 12.0f);
	assert(count >= 2U && count <= KWL_CUT_PIECES);

	/* One card's shadow (16 soft, 6 down) over its own card and its neighbours. */
	card_shadow[0] = 280.0f - 32.0f;
	card_shadow[1] = 26.0f - 32.0f;
	card_shadow[2] = 240.0f + 64.0f;
	card_shadow[3] = 180.0f + 64.0f;
	count = cut_check(card_shadow, cards, 8U, 12.0f);
	assert(count >= 4U && count <= KWL_CUT_PIECES);
	printf("cut: ok\n");
}

int
main(void)
{
	static struct kwl_server server;
	static struct kwl_object surface;
	static struct kwl_object role;
	int32_t cell[4] = { 648, 142, 752, 251 };
	int32_t all[4] = { 0, 0, INT32_MAX, INT32_MAX };
	int32_t left_edge[4] = { -20, -5, 30, 40 };
	int32_t outside[4] = { 2000, 900, 2100, 1000 };
	int32_t corner[4] = { 1270, 790, 1290, 810 };

	/* A mapped toplevel, Calendar's size, at its place on the output. */
	role.top = &role;
	surface.role = &role;
	surface.mapped = 1;
	surface.current = &new_image;
	surface.x = 418;
	surface.y = 236;
	old_width = 1280;
	old_height = 800;
	new_width = 1280;
	new_height = 800;
	body[0] = 418;
	body[1] = 236;
	body[2] = 418 + 1280;
	body[3] = 236 + 800;

	/* The glass look: a hovered cell alone, at the body's place. */
	server.glass = 1;
	commit(&server, &surface, cell);
	expect(&server, 418 + 648, 236 + 142, 418 + 752, 236 + 251);

	/* No damage named: the whole body. */
	commit(&server, &surface, NULL);
	expect(&server, 418, 236, 1698, 1036);

	/* The whole image named as the largest rectangle: the whole body. */
	commit(&server, &surface, all);
	expect(&server, 418, 236, 1698, 1036);

	/* Damage reaching outside the image is kept inside it. */
	commit(&server, &surface, left_edge);
	expect(&server, 418, 236, 418 + 30, 236 + 40);
	commit(&server, &surface, corner);
	expect(&server, 418 + 1270, 236 + 790, 1698, 1036);

	/* Damage wholly outside the image: the whole body. */
	commit(&server, &surface, outside);
	expect(&server, 418, 236, 1698, 1036);

	/* A body drawn at another size (a viewport): the whole body. */
	body[2] = 418 + 640;
	body[3] = 236 + 400;
	commit(&server, &surface, cell);
	expect(&server, 418, 236, 418 + 640, 236 + 400);
	body[2] = 418 + 1280;
	body[3] = 236 + 800;

	/* The plain look: the part at the window's place. */
	server.glass = 0;
	commit(&server, &surface, cell);
	expect(&server, 418 + 648, 236 + 142, 418 + 752, 236 + 251);
	commit(&server, &surface, NULL);
	expect(&server, 418, 236, 1698, 1036);

	/* A new size: the whole output. */
	server.glass = 1;
	new_width = 1000;
	commit(&server, &surface, cell);
	assert(server.dirty);

	/* Two windows' commits far apart: two rectangles. */
	server.dirty = 0;
	server.damaged = 0;
	server.damage_count = 3;
	kwl_damage_rect(&server, 0, 0, 50, 50);
	kwl_damage_rect(&server, 900, 900, 950, 950);
	assert(server.damaged && server.damage_count == 2);

	/* The rectangle list. */
	test_rects();

	/* The cut around opaque shapes. */
	test_cut();

	printf("damage-host-test: PASS\n");
	return 0;
}
