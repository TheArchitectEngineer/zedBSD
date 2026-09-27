/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The damage of window mode (ws035-p055, compositing design D4): the part
 * of the output a change needs drawn again.
 *
 * Every change zdesktop does not know the extent of marks the whole
 * output (server->dirty, as before).  Two frequent changes are measured
 * instead: the pointer moving over the windows' own areas (the cursor's
 * old and new places) and a window's new image when nothing near it is
 * glass (its body).  They gather in server->damage until a frame is drawn;
 * compose.c widens it by the frames each swapchain image missed (its
 * buffer age) and draws the frame inside it alone.
 */

#include "zwl.h"

/* How far around the pointer a cursor may draw (zdesktop's shapes and arrow are smaller). */
#define DAMAGE_CURSOR		64

static void damage_add(struct zwl_server *server, int32_t left, int32_t top, int32_t right, int32_t bottom);

/*
 * Marks the damage of the pointer's move from an old place: the cursor's
 * old and new places, when nothing zdesktop draws follows the pointer
 * there; otherwise the whole output.
 */
void
zwl_damage_pointer(
	struct zwl_server *server,
	int32_t old_x,
	int32_t old_y)
{
	int calm;

	/* A client's own cursor surface may be of any size. */
	if (server->cursor_surface != NULL) {
		server->dirty = 1;
		return;
	}

	/* In the glass look both places must be over windows' own areas, in a still look. */
	if (server->glass) {
		calm = zwl_glass_pointer_calm(server, old_x, old_y);
		if (calm)
			calm = zwl_glass_pointer_calm(server, server->pointer_x, server->pointer_y);
		if (!calm) {
			server->dirty = 1;
			return;
		}
	}

	/* The cursor where it was and where it is. */
	damage_add(server, old_x - DAMAGE_CURSOR, old_y - DAMAGE_CURSOR, old_x + DAMAGE_CURSOR, old_y + DAMAGE_CURSOR);
	damage_add(server, server->pointer_x - DAMAGE_CURSOR, server->pointer_y - DAMAGE_CURSOR, server->pointer_x + DAMAGE_CURSOR, server->pointer_y + DAMAGE_CURSOR);
}

/*
 * Marks the damage of a surface's new image (its commit, adopted with the
 * image it replaced): the window's body when a toplevel's image of the
 * same size replaced another and nothing near is glass; otherwise the
 * whole output.
 */
void
zwl_damage_commit(
	struct zwl_server *server,
	struct zwl_object *surface,
	struct zwl_object *previous)
{
	int32_t rect[4];
	uint32_t old_width;
	uint32_t old_height;
	uint32_t width;
	uint32_t height;
	int alone;

	/* A first image, an image taken away, or a sub-surface's, a popup's or a cursor's: everything. */
	if (previous == NULL || surface->current == NULL || surface->sub_role != NULL || surface->cursor_role) {
		server->dirty = 1;
		return;
	}

	/* Only a mapped toplevel's window, not covering the output, is drawn alone. */
	if (surface->role == NULL || surface->role->top == NULL || !surface->mapped || surface->fullscreen) {
		server->dirty = 1;
		return;
	}

	/* An image of another size changes the window's place on the output. */
	zwl_buffer_size(previous, &old_width, &old_height);
	zwl_buffer_size(surface->current, &width, &height);
	if (old_width != width || old_height != height) {
		server->dirty = 1;
		return;
	}

	/* The glass look: the body, when no window near it is glass (shell.c). */
	if (server->glass) {
		alone = zwl_glass_body_damage(server, surface, rect);
		if (!alone) {
			server->dirty = 1;
			return;
		}

		/* The body is all. */
		damage_add(server, rect[0], rect[1], rect[2], rect[3]);
		return;
	}

	/* The plain look: the window at its place, as large as its image. */
	damage_add(server, surface->x, surface->y, surface->x + (int32_t)width, surface->y + (int32_t)height);
}

/* Adds a rectangle (left, top, right, bottom) to the damage the next frame draws. */
static void
damage_add(
	struct zwl_server *server,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom)
{
	/* The first rectangle is the damage. */
	if (!server->damaged) {
		server->damage[0] = left;
		server->damage[1] = top;
		server->damage[2] = right;
		server->damage[3] = bottom;
		server->damaged = 1;
		return;
	}

	/* Later ones widen it to the box around both. */
	if (left < server->damage[0])
		server->damage[0] = left;
	if (top < server->damage[1])
		server->damage[1] = top;
	if (right > server->damage[2])
		server->damage[2] = right;
	if (bottom > server->damage[3])
		server->damage[3] = bottom;
}
