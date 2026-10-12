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
 */

#include "userland/desktop/wayland/compose.h"
#include "userland/desktop/wayland/glass.h"
#include "userland/desktop/wayland/notify-view.h"
#include "userland/desktop/wayland/popup.h"

#include <assert.h>
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
	memset(server->damage, 0, sizeof(server->damage));
	surface->committed_damaged = 0;
	if (damage != NULL) {
		memcpy(surface->committed_damage, damage, sizeof(surface->committed_damage));
		surface->committed_damaged = 1;
	}
	kwl_damage_commit(server, surface, &old_image);
}

/* Checks that the frame draws exactly a rectangle (left, top, right, bottom). */
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
	if (server->damage[0] != left ||
	    server->damage[1] != top ||
	    server->damage[2] != right ||
	    server->damage[3] != bottom) {
		printf("damage %d,%d,%d,%d expected %d,%d,%d,%d\n", server->damage[0], server->damage[1], server->damage[2], server->damage[3], left, top, right,
		       bottom);
		assert(0);
	}
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

	printf("damage-host-test: PASS\n");
	return 0;
}
