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
 * Every change the compositor does not know the extent of marks the whole
 * output (server->dirty, as before).  Two frequent changes are measured
 * instead: the pointer moving over the windows' own areas (the cursor's
 * old and new places) and a window's new image when nothing near it is
 * glass (its body).  They gather in server->damage until a frame is drawn;
 * compose.c widens it by the frames each swapchain image missed (its
 * buffer age) and draws the frame inside it alone.
 */

#include "kwl.h"
#include "compose.h"
#include "glass.h"
#include "notify-view.h"
#include "popup.h"

/* How far around the pointer a cursor may draw (the compositor's shapes and arrow are smaller). */
#define DAMAGE_CURSOR		64

/* How far a hovered thing's light and the glass's blur over it reach beyond its probe. */
#define DAMAGE_HOVER		96

static void damage_add(struct kwl_server *server, int32_t left, int32_t top, int32_t right, int32_t bottom);
static int cursor_unseen(struct kwl_server *server, int32_t x, int32_t y);
static int probes_changed(struct kwl_server *server);
static int hover_record_usable(struct kwl_server *server);
static int probes_damaged(struct kwl_server *server);
static int probe_contains(const struct kwl_hover_probe *probe, int32_t x, int32_t y);

/*
 * Marks the damage of the pointer's move from an old place: the cursor's
 * old and new places, when nothing the compositor draws follows the pointer
 * there; otherwise the whole output.
 */
void
kwl_damage_pointer(
	struct kwl_server *server,
	int32_t old_x,
	int32_t old_y)
{
	int unseen;
	int calm;
	int plane;

	/*
	 * A cursor the window's client hid, over that client's window at both
	 * places, draws nothing (ws099-p015: the pen over fullscreen Notes
	 * would otherwise redraw the output for each of its places).
	 */
	unseen = cursor_unseen(server, old_x, old_y);
	if (unseen)
		unseen = cursor_unseen(server, server->pointer_x, server->pointer_y);
	if (unseen)
		return;

	/* Remember the latest accepted motion without writing a log for every input report. */
	if (server->log_frames)
		server->pointer_accepted_us = kwl_microseconds();

	/* The cursor plane shows the move by itself (cursor-plane.c); a frame is drawn only for what the move changes under it. */
	plane = kwl_cursor_plane_takes(server);

	/* The next frame shows the move without waiting for the windows (frame pacing, display.c). */
	if (!plane)
		server->pointer_moved = 1U;

	/* A client's own cursor surface may be of any size, and so may a drag's icon. */
	if (!plane && (server->cursor_surface != NULL || server->dnd_active)) {
		server->dirty = 1;
		return;
	}

	/*
	 * In the glass look both places must be over windows' own areas, in a
	 * still look; elsewhere a move that changes no hover answer the scene
	 * was drawn with changes only the cursor.
	 */
	if (server->glass) {
		calm = kwl_glass_pointer_calm(server, old_x, old_y);
		if (calm)
			calm = kwl_glass_pointer_calm(server, server->pointer_x, server->pointer_y);
		if (!calm)
			calm = kwl_hover_damage(server);
		if (!calm) {
			server->dirty = 1;
			return;
		}
	}

	/* The plane moves the cursor; the frame keeps its pixels. */
	if (plane)
		return;

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
kwl_damage_commit(
	struct kwl_server *server,
	struct kwl_object *surface,
	struct kwl_object *previous)
{
	int32_t rect[4];
	uint32_t old_width;
	uint32_t old_height;
	uint32_t width;
	uint32_t height;
	int alone;

	/* Client pixels changed even when their measured damage is only a window body. */
	if (server->compose != NULL)
		server->compose->scene.changed = 1U;

	/* A cursor's new image is the cursor plane's alone while the plane shows it (cursor-plane.c). */
	if (surface->cursor_role && kwl_cursor_plane_takes(server))
		return;

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
	kwl_buffer_size(previous, &old_width, &old_height);
	kwl_buffer_size(surface->current, &width, &height);
	if (old_width != width || old_height != height) {
		server->dirty = 1;
		return;
	}

	/* The glass look: the body, when no window near it is glass (shell.c). */
	if (server->glass) {
		alone = kwl_glass_body_damage(server, surface, rect);
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


/*
 * Records one rectangle a whole scene's drawing tests the pointer against.
 *
 * Drawing that lights something under the pointer calls this with the
 * rectangle it tests, while the compositor records a whole scene; outside
 * that recording it does nothing.
 */
void
kwl_hover_probe(
	struct kwl_server *server,
	int32_t x,
	int32_t y,
	int32_t width,
	int32_t height)
{
	struct kwl_hover_probe *probe;

	/* Only a whole scene's drawing records. */
	if (!server->hover_recording)
		return;

	/* A full list makes the record incomplete, so moves redraw as before. */
	if (server->hover_count == KWL_HOVER_PROBES) {
		server->hover_overflow = 1U;
		return;
	}
	probe = &server->hover_probes[server->hover_count];
	server->hover_count++;
	probe->x = x;
	probe->y = y;
	probe->width = width;
	probe->height = height;
}

/*
 * Starts or abandons the hover record of the frame being drawn.
 *
 * A whole scene starts a new record at the pointer's place; a partial
 * redraw leaves parts drawn at other places, so the record is dropped.
 */
void
kwl_hover_begin(
	struct kwl_server *server,
	int whole)
{
	/*
	 * A partial redraw draws every hover answer as the record has it when
	 * no probe changed, so the record stays valid; otherwise it is unusable
	 * until a whole scene finishes a new one.
	 */
	server->hover_recording = 0U;
	if (!whole) {
		if (server->hover_complete && !probes_changed(server))
			return;

		/* Probes whose answer changed inside the damage are drawn for the pointer's place now. */
		if (server->hover_complete && probes_damaged(server)) {
			server->hover_x = server->pointer_x;
			server->hover_y = server->pointer_y;
			return;
		}
		server->hover_complete = 0U;
		return;
	}
	server->hover_complete = 0U;

	/* The new record starts empty at the place the scene is drawn for. */
	server->hover_count = 0U;
	server->hover_overflow = 0U;
	server->hover_x = server->pointer_x;
	server->hover_y = server->pointer_y;
	server->hover_recording = 1U;
}

/*
 * Ends the hover record of a whole scene; it holds every probe unless the list overflowed.
 */
void
kwl_hover_end(
	struct kwl_server *server)
{
	/* A record that ran out of room cannot vouch for an unchanged scene. */
	if (server->hover_recording && !server->hover_overflow)
		server->hover_complete = 1U;
	server->hover_recording = 0U;
}
/*
 * Tells whether the scene drawn for the recorded pointer place looks the same with the pointer where it is now.
 *
 * Only a still look with no dialog, switcher or held button qualifies, and
 * every recorded probe must give the same answer at both places.
 */
int
kwl_hover_unchanged(
	struct kwl_server *server)
{
	int open;

	/* A record that can vouch for the scene. */
	open = hover_record_usable(server);
	if (!open)
		return 0;

	/* Every probe answers as it did when the scene was drawn. */
	open = probes_changed(server);
	if (open)
		return 0;

	/* Succeeded: only the cursor differs. */
	return 1;
}

/*
 * Marks the probes whose answer the pointer changed since the scene was drawn, with the reach of their light, as damage.
 *
 * Returns 0, marking nothing, when the record cannot vouch for the rest of
 * the scene; the caller then redraws the whole output.
 */
int
kwl_hover_damage(
	struct kwl_server *server)
{
	const struct kwl_hover_probe *probe;
	unsigned index;
	int usable;
	int before;
	int after;

	/* A record that can vouch for the scene. */
	usable = hover_record_usable(server);
	if (!usable)
		return 0;

	/* Each probe that answers differently, and the light around it. */
	for (index = 0; index < server->hover_count; index++) {
		probe = &server->hover_probes[index];
		before = probe_contains(probe, server->hover_x, server->hover_y);
		after = probe_contains(probe, server->pointer_x, server->pointer_y);
		if (before == after)
			continue;

		/* The scene itself changes there: the cursor-free copy of it is stale (compose.c). */
		if (server->compose != NULL)
			server->compose->scene.changed = 1U;
		damage_add(server,
			   probe->x - DAMAGE_HOVER,
			   probe->y - DAMAGE_HOVER,
			   probe->x + probe->width + DAMAGE_HOVER,
			   probe->y + probe->height + DAMAGE_HOVER);
	}

	/* Succeeded: the rest of the scene stays as drawn. */
	return 1;
}

/* Tells whether the hover record can vouch for the scene apart from its probes. */
static int
hover_record_usable(
	struct kwl_server *server)
{
	int open;

	/* A whole scene's complete record is needed. */
	if (!server->hover_complete)
		return 0;

	/* Moving, dragging and held buttons keep redrawing. */
	if (server->buttons_down != 0U)
		return 0;
	open = kwl_glass_still(server);
	if (!open)
		return 0;

	/* Dialogs and panels draw their own hover without probes. */
	open = kwl_power_dialog_showing(server);
	if (open)
		return 0;
	open = kwl_bluetooth_ask_showing();
	if (open)
		return 0;
	open = kwl_arrange_showing(server);
	if (open)
		return 0;
	open = kwl_notify_log_showing();
	if (open)
		return 0;
	if (server->switcher.on)
		return 0;

	/* Succeeded: only the probes can differ. */
	return 1;
}

/* Tells whether every probe that answers differently now lies inside the damage the next frame draws. */
static int
probes_damaged(
	struct kwl_server *server)
{
	const struct kwl_hover_probe *probe;
	unsigned index;
	int before;
	int after;

	/* No damage holds nothing. */
	if (!server->damaged)
		return 0;

	/* Each changed probe, with its reach, inside the damage's box. */
	for (index = 0; index < server->hover_count; index++) {
		probe = &server->hover_probes[index];
		before = probe_contains(probe, server->hover_x, server->hover_y);
		after = probe_contains(probe, server->pointer_x, server->pointer_y);
		if (before == after)
			continue;
		if (probe->x - DAMAGE_HOVER < server->damage[0] ||
		    probe->y - DAMAGE_HOVER < server->damage[1] ||
		    probe->x + probe->width + DAMAGE_HOVER > server->damage[2] ||
		    probe->y + probe->height + DAMAGE_HOVER > server->damage[3])
			return 0;
	}

	/* Succeeded: the frame draws every changed answer. */
	return 1;
}

/* Adds a rectangle (left, top, right, bottom) to the damage the next frame draws. */
static void
damage_add(
	struct kwl_server *server,
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

/*
 * Tells whether the cursor draws nothing at a point: the glass look's
 * still windows, the client that hid its cursor (with no resize arrow, no
 * drag, no popup's grab) owning the window body there (compose.c's
 * cursor, BUG-118).
 */
static int
cursor_unseen(
	struct kwl_server *server,
	int32_t x,
	int32_t y)
{
	struct kwl_object *window;
	int calm;

	/* A hidden client's cursor, and nothing else that follows the pointer. */
	if (!server->glass ||
	    server->cursor_client == NULL ||
	    !server->cursor_hidden ||
	    server->cursor_surface != NULL ||
	    server->frame_edges != 0U ||
	    server->dnd_active ||
	    server->pointer_grabbed)
		return 0;

	/* Over a window's body in a still look. */
	calm = kwl_glass_pointer_calm(server, x, y);
	if (!calm)
		return 0;

	/* The client's own window. */
	window = kwl_glass_body_at(server, x, y);
	if (window == NULL || window->client != server->cursor_client)
		return 0;

	/* Succeeded: nothing is drawn there. */
	return 1;
}

/* Tells whether any recorded probe answers differently with the pointer where it is now. */
static int
probes_changed(
	struct kwl_server *server)
{
	unsigned index;
	int before;
	int after;

	/* Each probe, at the recorded place and at the pointer. */
	for (index = 0; index < server->hover_count; index++) {
		before = probe_contains(&server->hover_probes[index], server->hover_x, server->hover_y);
		after = probe_contains(&server->hover_probes[index], server->pointer_x, server->pointer_y);
		if (before != after)
			return 1;
	}

	/* None differs. */
	return 0;
}

/* Tells whether a probe's rectangle holds a point. */
static int
probe_contains(
	const struct kwl_hover_probe *probe,
	int32_t x,
	int32_t y)
{
	/* Left of or right of it. */
	if (x < probe->x || x >= probe->x + probe->width)
		return 0;

	/* Above or below it. */
	if (y < probe->y || y >= probe->y + probe->height)
		return 0;

	/* Inside. */
	return 1;
}

/* Marks a rectangle (left, top, right, bottom) of the output as damage, for changes measured elsewhere (a window's move, shell.c). */
void
kwl_damage_rect(
	struct kwl_server *server,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom)
{
	/* The scene itself changes there: the cursor-free copy of it is stale (compose.c). */
	if (server->compose != NULL)
		server->compose->scene.changed = 1U;

	/* Gathered with the rest of the frame's damage. */
	damage_add(server, left, top, right, bottom);
}
