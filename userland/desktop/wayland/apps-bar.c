/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The applications' icons in the system bar, and their window previews
 * (ws142-p004, the 2026-10-04 user request and the decisions D2, D4 to D8
 * and D11 of plan/ws142/phase001).
 *
 * While no window is docked (D6: a docked window's title has the bar; a
 * fullscreen window hides the bar), the bar shows an icon for each
 * application on the desktop shown (apps.c), from after the launcher's
 * line to before the desktops' line, in the desktop's own order: the order
 * the applications were opened in, which dragging an icon changes (the
 * 2026-10-05 request; each desktop keeps its order).  The application of
 * the window on top has a short line under its icon (all the icons sit in
 * one pill, ws099-p034); one whose windows are all
 * minimized is drawn faint (D11).  When there are more applications than
 * room, the last place is "+N", which opens Wiseview.
 *
 * The pointer resting on an icon for HOVER_MS shows the application's
 * windows as previews (at most a quarter of the output's width and height
 * each, D4) in a glass panel under the bar; moving to another icon shows
 * its windows at once; leaving the icon and the panel for LEAVE_MS hides
 * them.  A click on the icon of an application with one window brings that
 * window to the top (back from minimized); with more it shows the previews
 * at once, and a second click hides them.  A click on a preview brings its
 * window; on its close button closes it.  Esc, and a press elsewhere, hide
 * the previews (the press goes on to what is under it).
 */

#include "zwl.h"
#include "apps.h"
#include "apps-bar.h"
#include "desktop.h"
#include "glass.h"
#include "extras.h"
#include "keyboard.h"

#include <stdio.h>
#include <string.h>

/* The pointer's rest on an icon before the previews show, and its absence before they go (milliseconds; D8). */
#define HOVER_MS		400U
#define LEAVE_MS		300U

/* How far a press moves before it is the icon's drag (pixels). */
#define DRAG_START		8

/* The key of the "+N" place. */
#define MORE_KEY		"+"

/* What a slot of the bar is: an application, the "+N" place, or none. */
#define SLOT_NONE		(-1)
#define SLOT_MORE		(-2)

static int slot_at(const struct apps_view *view, int32_t x, int32_t y);
static void slot_rect(const struct apps_view *view, unsigned slot, struct apps_rect *rect);
static int panel_build(struct zwl_server *server, const struct apps_view *view, const char *key, struct apps_panel *panel);
static void log_bar(struct zwl_server *server, const struct apps_view *view);
static void draw_more(struct zwl_server *server, VkCommandBuffer command, const struct apps_rect *rect, unsigned hidden, float light);
static void draw_light(struct zwl_server *server, VkCommandBuffer command, const struct apps_rect *rect, float strength);

/*
 * Draws the applications' icons in the system bar (draw_system_bar, when no window is docked).  Returns 1 when it drew any.
 */
int
zwl_apps_bar_draw(
	struct zwl_server *server,
	VkCommandBuffer command)
{
	static const float underline[4] = { 1.0f, 1.0f, 1.0f, 0.94f };
	static const float fill[4] = { 0.0f, 0.0f, 0.0f, 0.25f };
	struct glass_shape shape;
	struct zwl_apps_bar *state;
	struct apps_view view;
	struct apps_rect rect;
	struct zwl_object *surface;
	const struct zwl_app *app;
	unsigned slot;
	unsigned slots;
	int32_t x;
	int32_t pill_x;
	int32_t pill_width;
	float alpha;
	float light;
	int same;
	int built;

	/* No icons where the bar has no room for them. */
	state = &server->apps_bar;
	built = zwl_apps_view_build(server, &view);
	if (!built)
		return 0;

	/* The bar as it is now, in the log when it changed. */
	log_bar(server, &view);

	/* One pill behind all the icons shown and the "+N" place (ws099-p034). */
	slots = view.shown;
	if (view.hidden > 0U)
		slots++;
	pill_x = view.left + (ICON_WIDTH - ICON_MARK) / 2 - ICON_PILL_PAD;
	pill_width = (int32_t)slots * ICON_WIDTH - (ICON_WIDTH - ICON_MARK) + 2 * ICON_PILL_PAD;
	glass_draw_solid(server, command, (float)pill_x, (float)(ZWL_GLASS_BAR / 2 - 17), (float)pill_width, 34.0f, 17.0f, fill);
	glass_shape_init(&shape, (float)pill_x, (float)(ZWL_GLASS_BAR / 2 - 17), (float)pill_width, 34.0f);
	shape.mode = MODE_RING;
	shape.radius = 17.0f;
	shape.soft = 1.0f;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = 0.12f;
	glass_shape_draw(server, command, &shape);

	/* Each application's icon. */
	for (slot = 0; slot < view.shown; slot++) {
		app = &view.apps.apps[slot];
		slot_rect(&view, slot, &rect);

		/* Lit while the pointer rests on it or its previews show. */
		light = 0.0f;
		same = strcmp(app->key, state->key);
		if (same == 0 && state->state == ZWL_APPS_SHOWN)
			light = 1.0f;
		if (same == 0 && state->state == ZWL_APPS_ARMED)
			light = 0.6f;

		/* The icon being dragged follows the pointer. */
		same = strcmp(app->key, state->press_key);
		x = rect.x;
		if (state->dragging && same == 0) {
			x = server->pointer_x - ICON_WIDTH / 2;
			light = 1.0f;
		}

		/* Its light. */
		rect.x = x;
		if (light > 0.0f)
			draw_light(server, command, &rect, light);

		/* Its mark, faint when all its windows are minimized (D11). */
		surface = view.surfaces[app->windows[0]];
		alpha = 1.0f;
		if (app->minimized)
			alpha = 0.45f;
		zwl_glass_draw_app_mark(server, command, surface, x + (ICON_WIDTH - ICON_MARK) / 2, ZWL_GLASS_BAR / 2, ICON_MARK, alpha);

		/* A short line under the application of the window on top (ws099-p034). */
		if ((int)slot == view.current)
			glass_draw_solid(server, command, (float)(x + ICON_WIDTH / 2 - 4), (float)(ZWL_GLASS_BAR / 2 + 14), 8.0f, 2.5f, 1.25f, underline);
	}

	/* The "+N" place for the applications without room. */
	if (view.hidden > 0U) {
		slot_rect(&view, view.shown, &rect);
		light = 0.0f;
		same = strcmp(state->press_key, MORE_KEY);
		if (state->pressed && same == 0)
			light = 1.0f;
		draw_more(server, command, &rect, view.hidden, light);
	}

	/* Succeeded: drawn. */
	return 1;
}

/*
 * Draws the panel of previews, when it shows (over the windows, under the menus).
 */
void
zwl_apps_bar_draw_popup(
	struct zwl_server *server,
	VkCommandBuffer command)
{
	static const float faint[4] = { 1.0f, 1.0f, 1.0f, 0.5f };
	struct zwl_apps_bar *state;
	struct apps_view view;
	struct apps_panel panel;
	struct glass_shape shape;
	unsigned index;
	int over;
	int built;

	/* Only while the previews show, and the bar still has its icons. */
	state = &server->apps_bar;
	if (state->state != ZWL_APPS_SHOWN)
		return;
	built = zwl_apps_view_build(server, &view);
	if (built)
		built = panel_build(server, &view, state->key, &panel);
	if (!built)
		return;

	/* The panel: a shadow, then glass. */
	glass_shape_init(&shape, (float)panel.rect.x, (float)panel.rect.y + 6.0f, (float)panel.rect.width, (float)panel.rect.height);
	shape.quad[0] -= 32.0f;
	shape.quad[1] -= 32.0f;
	shape.quad[2] += 64.0f;
	shape.quad[3] += 64.0f;
	shape.mode = MODE_SHADOW;
	shape.radius = 18.0f;
	shape.soft = 20.0f;
	shape.color[0] = 0.10f;
	shape.color[1] = 0.18f;
	shape.color[2] = 0.35f;
	shape.color[3] = 0.25f;
	glass_shape_draw(server, command, &shape);
	glass_shape_init(&shape, (float)panel.rect.x, (float)panel.rect.y, (float)panel.rect.width, (float)panel.rect.height);
	shape.mode = MODE_GLASS;
	shape.radius = 18.0f;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = 0.62f;
	shape.edge = 0.8f;
	glass_shape_draw(server, command, &shape);

	/* Each window's preview, the one under the pointer lit, a minimized one faint (D11). */
	over = zwl_apps_tile_at(panel.tiles, panel.count, server->pointer_x, server->pointer_y);
	for (index = 0; index < panel.count; index++) {
		zwl_glass_draw_preview(server, command, panel.surfaces[index], panel.tiles[index].x, panel.tiles[index].y, panel.tiles[index].width, panel.tiles[index].height, over == (int)index);
		if (panel.surfaces[index]->minimized)
			glass_draw_solid(server, command, (float)panel.tiles[index].x, (float)panel.tiles[index].y, (float)panel.tiles[index].width, (float)panel.tiles[index].height, 10.0f, faint);
	}
}

/*
 * Follows the pointer: the rest on an icon that shows its previews, the
 * move to another icon, the leaving of the icons and the panel, and an
 * icon's drag.  Returns 1 when the motion is the bar's (the drag, or over
 * the panel).
 */
int
zwl_apps_bar_motion(
	struct zwl_server *server)
{
	struct zwl_apps_bar *state;
	struct zwl_apps_order *order;
	struct apps_view view;
	struct apps_panel panel;
	int32_t dx;
	int target;
	int from;
	int slot;
	int same;
	int built;
	int in_panel;

	/* Without the icons, nothing shows or waits. */
	state = &server->apps_bar;
	built = zwl_apps_view_build(server, &view);
	if (!built) {
		if (state->state != ZWL_APPS_IDLE)
			zwl_apps_bar_hide(server, "away");
		state->pressed = 0;
		state->dragging = 0;
		return 0;
	}

	/* The switcher's previews stay as it shows them; the motion over them is the bar's. */
	if (server->switcher.on) {
		in_panel = 0;
		if (state->state == ZWL_APPS_SHOWN) {
			built = panel_build(server, &view, state->key, &panel);
			if (built)
				in_panel = zwl_apps_inside(&panel.rect, server->pointer_x, server->pointer_y);
		}

		/* Elsewhere the motion goes on. */
		if (!in_panel)
			return 0;

		/* Over the previews the motion lights them. */
		server->dirty = 1;
		return 1;
	}

	/* A window's move or pull, or a desktop swipe, passing over the bar starts no wait. */
	if (server->drag != NULL ||
	    server->pull != NULL ||
	    server->desktop_press) {
		if (state->state == ZWL_APPS_ARMED)
			zwl_apps_bar_hide(server, "move");
		return 0;
	}

	/* A pressed icon past DRAG_START is dragged: it takes the place under the pointer, the others make room. */
	if (state->pressed) {
		dx = server->pointer_x - state->press_x;
		same = strcmp(state->press_key, MORE_KEY);
		if (!state->dragging &&
		    same != 0 &&
		    (dx >= DRAG_START || dx <= -DRAG_START)) {
			state->dragging = 1;
			zwl_apps_bar_hide(server, "drag");
			printf("ZWL APPS drag app=%s\n", state->press_key);
		}

		/* Its place follows the pointer. */
		if (state->dragging) {
			order = &state->orders[server->desktop];
			target = (server->pointer_x - view.left) / ICON_WIDTH;
			if (target < 0)
				target = 0;
			if (target >= (int)view.shown)
				target = (int)view.shown - 1;
			from = zwl_apps_find(&view.apps, state->press_key);
			if (from >= 0 &&
			    target >= 0 &&
			    from != target)
				(void)zwl_apps_move(order, (unsigned)from, (unsigned)target);
			server->dirty = 1;
		}

		/* The press's motion is the bar's. */
		return 1;
	}

	/* What is under the pointer: an icon, the panel. */
	slot = slot_at(&view, server->pointer_x, server->pointer_y);
	in_panel = 0;
	if (state->state == ZWL_APPS_SHOWN) {
		built = panel_build(server, &view, state->key, &panel);
		if (built)
			in_panel = zwl_apps_inside(&panel.rect, server->pointer_x, server->pointer_y);
	}

	/* Resting on nothing yet: an application's icon starts the wait. */
	if (state->state == ZWL_APPS_IDLE) {
		if (slot >= 0) {
			state->state = ZWL_APPS_ARMED;
			(void)snprintf(state->key, sizeof(state->key), "%s", view.apps.apps[slot].key);
			state->since_ms = zwl_milliseconds();
			server->dirty = 1;
		}

		/* The motion goes on. */
		return 0;
	}

	/* Waiting: another icon starts again, none stops. */
	if (state->state == ZWL_APPS_ARMED) {
		if (slot < 0) {
			state->state = ZWL_APPS_IDLE;
			state->key[0] = '\0';
			server->dirty = 1;
			return 0;
		}

		/* Another icon. */
		same = strcmp(view.apps.apps[slot].key, state->key);
		if (same != 0) {
			(void)snprintf(state->key, sizeof(state->key), "%s", view.apps.apps[slot].key);
			state->since_ms = zwl_milliseconds();
			server->dirty = 1;
		}

		/* The motion goes on. */
		return 0;
	}

	/* Shown: another icon shows its previews at once. */
	if (slot >= 0) {
		same = strcmp(view.apps.apps[slot].key, state->key);
		if (same != 0)
			zwl_apps_bar_show(server, &view, view.apps.apps[slot].key, ZWL_APPS_VIA_HOVER);
		state->left = 0;
		return 0;
	}

	/* Over the panel: it stays, and the motion lights its previews. */
	if (in_panel) {
		state->left = 0;
		server->dirty = 1;
		return 1;
	}

	/* Away from both: the time to go starts. */
	if (!state->left) {
		state->left = 1;
		state->since_ms = zwl_milliseconds();
	}

	/* The motion goes on. */
	return 0;
}

/*
 * Takes a button: a press on an icon (its click or drag decided at the
 * release), on a preview (brings or closes its window), and the release of
 * such a press.  A press elsewhere hides the previews and goes on.
 * Returns 1 when the button is the bar's.
 */
int
zwl_apps_bar_button(
	struct zwl_server *server,
	uint32_t button,
	uint32_t state_value)
{
	struct zwl_apps_bar *state;
	struct apps_view view;
	struct apps_panel panel;
	struct zwl_object *surface;
	const struct zwl_app *app;
	int found;
	int built;
	int slot;
	int tile;
	int same;
	int in_panel;

	/* The release of a press on an icon: the drag's end, or the click. */
	state = &server->apps_bar;
	if (state_value == 0) {
		if (!state->pressed || button != ZWL_BUTTON_LEFT)
			return 0;
		state->pressed = 0;
		built = zwl_apps_view_build(server, &view);

		/* A drag ends where it is. */
		if (state->dragging) {
			state->dragging = 0;
			found = -1;
			if (built)
				found = zwl_apps_find(&view.apps, state->press_key);
			printf("ZWL APPS reorder app=%s place=%d desktop=%u\n", state->press_key, found, server->desktop + 1U);
			server->dirty = 1;
			return 1;
		}

		/* The "+N" place opens Wiseview. */
		same = strcmp(state->press_key, MORE_KEY);
		if (same == 0) {
			zwl_apps_bar_hide(server, "more");
			zwl_glass_open_wiseview(server, "apps");
			return 1;
		}

		/* An application that has gone does nothing. */
		found = -1;
		if (built)
			found = zwl_apps_find(&view.apps, state->press_key);
		if (found < 0)
			return 1;

		/* One window: it comes to the top. */
		app = &view.apps.apps[found];
		if (app->window_count == 1U) {
			zwl_apps_bar_hide(server, "raise");
			zwl_glass_bring(server, view.surfaces[app->windows[0]], "bar");
			return 1;
		}

		/* More: a second click on the shown icon hides its previews, otherwise they show at once. */
		same = strcmp(state->key, app->key);
		if (state->state == ZWL_APPS_SHOWN &&
		    state->via == ZWL_APPS_VIA_CLICK &&
		    same == 0) {
			zwl_apps_bar_hide(server, "click");
			return 1;
		}

		/* Shown by the click. */
		zwl_apps_bar_show(server, &view, app->key, ZWL_APPS_VIA_CLICK);
		return 1;
	}

	/* Without the icons the bar takes nothing. */
	built = zwl_apps_view_build(server, &view);
	if (!built) {
		if (state->state != ZWL_APPS_IDLE)
			zwl_apps_bar_hide(server, "away");
		return 0;
	}

	/* The previews take a press on themselves: a preview's close button closes its window, the preview brings it. */
	if (state->state == ZWL_APPS_SHOWN) {
		built = panel_build(server, &view, state->key, &panel);
		in_panel = 0;
		if (built)
			in_panel = zwl_apps_inside(&panel.rect, server->pointer_x, server->pointer_y);
		if (in_panel) {
			tile = zwl_apps_tile_at(panel.tiles, panel.count, server->pointer_x, server->pointer_y);
			if (tile < 0 || button != ZWL_BUTTON_LEFT)
				return 1;
			surface = panel.surfaces[tile];

			/* The close button at the preview's top right. */
			if (server->pointer_x >= panel.tiles[tile].x + panel.tiles[tile].width - 26 && server->pointer_y < panel.tiles[tile].y + 26) {
				(void)zwl_emit(surface->client, surface->role->top->id, 1U, NULL, 0U);
				printf("ZWL APPS close-window surface=%u client=%llu\n", surface->id, (unsigned long long)surface->client->number);
				return 1;
			}

			/* The preview: its window comes to the top. */
			zwl_apps_bar_hide(server, "preview");
			zwl_glass_bring(server, surface, "preview");
			return 1;
		}
	}

	/* A left press on an icon or the "+N" place waits for its release. */
	slot = slot_at(&view, server->pointer_x, server->pointer_y);
	if (slot != SLOT_NONE && button == ZWL_BUTTON_LEFT) {
		state->pressed = 1;
		state->dragging = 0;
		state->press_x = server->pointer_x;
		(void)snprintf(state->press_key, sizeof(state->press_key), "%s", MORE_KEY);
		if (slot >= 0)
			(void)snprintf(state->press_key, sizeof(state->press_key), "%s", view.apps.apps[slot].key);
		return 1;
	}

	/* A press elsewhere hides the previews, and goes on. */
	if (state->state != ZWL_APPS_IDLE)
		zwl_apps_bar_hide(server, "press");
	return 0;
}

/*
 * Takes a key: Esc hides the previews.  Returns 1 when the key was the bar's.
 */
int
zwl_apps_bar_key(
	struct zwl_server *server,
	uint32_t key,
	uint32_t state_value)
{
	/* Only Esc's press while the previews show or wait. */
	if (key != ZWL_KEY_ESC ||
	    state_value == 0U ||
	    server->apps_bar.state == ZWL_APPS_IDLE)
		return 0;

	/* A wait only stops (the key goes on); shown previews hide and take it. */
	if (server->apps_bar.state == ZWL_APPS_ARMED) {
		zwl_apps_bar_hide(server, "escape");
		return 0;
	}

	/* Hidden. */
	zwl_apps_bar_hide(server, "escape");
	return 1;
}

/*
 * Lets time pass: a rest long enough shows the previews, an absence long enough hides them.
 */
void
zwl_apps_bar_tick(
	struct zwl_server *server)
{
	struct zwl_apps_bar *state;
	struct apps_view view;
	uint64_t now;
	int built;

	/* Nothing waits while idle. */
	state = &server->apps_bar;
	if (state->state == ZWL_APPS_IDLE)
		return;

	/* The icons gone (a window docked, Home, Wiseview, a fullscreen window): nothing shows. */
	built = zwl_apps_view_build(server, &view);
	if (!built) {
		zwl_apps_bar_hide(server, "away");
		return;
	}

	/* The rest on an icon. */
	now = zwl_milliseconds();
	if (state->state == ZWL_APPS_ARMED && now - state->since_ms >= HOVER_MS) {
		zwl_apps_bar_show(server, &view, state->key, ZWL_APPS_VIA_HOVER);
		return;
	}

	/* The absence from the icons and the panel hides what the rest showed (a click's previews stay). */
	if (state->state == ZWL_APPS_SHOWN &&
	    state->via == ZWL_APPS_VIA_HOVER &&
	    state->left &&
	    now - state->since_ms >= LEAVE_MS)
		zwl_apps_bar_hide(server, "leave");
}

/*
 * Gathers the bar's applications and their icons' places.  Returns 0 when
 * the bar has no room for icons now, or no application.
 */
int
zwl_apps_view_build(
	struct zwl_server *server,
	struct apps_view *view)
{
	unsigned slots;
	int room;
	int collected;

	/* Room in the bar. */
	room = zwl_glass_apps_room(server, &view->left, &view->right);
	if (!room)
		return 0;

	/* The applications. */
	collected = zwl_apps_view_collect(server, view);
	if (!collected)
		return 0;

	/* As many icons as there is room for; the last place says how many more when they do not fit. */
	slots = 0;
	if (view->right > view->left)
		slots = (unsigned)((view->right - view->left) / ICON_WIDTH);
	if (slots == 0U)
		return 0;
	view->shown = view->apps.count;
	view->hidden = 0;
	if (view->apps.count > slots) {
		view->shown = slots - 1U;
		view->hidden = view->apps.count - view->shown;
	}

	/* The current application only when its icon is shown. */
	if (view->current >= (int)view->shown)
		view->current = -1;

	/* Succeeded: there are icons. */
	return 1;
}

/*
 * Gathers the applications of the desktop shown (in its bar order) and the
 * application of the window on top, without the icons' places (the
 * switcher in the middle of the output needs no room in the bar).  Returns
 * 0 when there is none.
 */
int
zwl_apps_view_collect(
	struct zwl_server *server,
	struct apps_view *view)
{
	struct zwl_client *client;
	struct zwl_object *surface;
	struct zwl_object *parent;
	struct zwl_object *top;
	struct zwl_apps_window *window;
	unsigned index;
	unsigned app;
	int desktop_surface;

	/* A desktop that keeps an order. */
	view->shown = 0;
	view->hidden = 0;
	view->current = -1;
	view->window_count = 0;
	view->apps.count = 0;
	if (server->desktop >= ZWL_APPS_DESKTOPS)
		return 0;

	/* The windows of the desktop shown, as Wiseview has them (a sheet comes with its parent). */
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			/* Only a window with an image, of the desktop shown. */
			if (surface->kind != ZWL_SURFACE ||
			    surface->dead ||
			    !surface->mapped ||
			    surface->role == NULL ||
			    surface->role->top == NULL ||
			    surface->cursor_role ||
			    surface->current == NULL ||
			    surface->desktop != server->desktop)
				continue;
			parent = zwl_sheet_parent(surface);
			if (parent != NULL || view->window_count >= VIEW_WINDOWS)
				continue;

			/* The desktop's icons are no application (desktop.c). */
			desktop_surface = zwl_desktop_is(surface);
			if (desktop_surface)
				continue;

			/* Described for apps.c. */
			window = &view->described[view->window_count];
			window->app_id = surface->app_id;
			window->client = client->number;
			window->open_order = surface->open_order;
			window->map_order = surface->map_order;
			window->minimized = surface->minimized;
			view->surfaces[view->window_count] = surface;
			view->window_count++;
		}
	}

	/* The applications, in the desktop's order. */
	zwl_apps_build(view->described, view->window_count, &server->apps_bar.orders[server->desktop], &view->apps);
	if (view->apps.count == 0U)
		return 0;

	/* The application of the window on top. */
	top = zwl_top_window(server);
	parent = zwl_sheet_parent(top);
	if (parent != NULL)
		top = parent;
	for (app = 0; app < view->apps.count && top != NULL; app++) {
		for (index = 0; index < view->apps.apps[app].window_count; index++) {
			if (view->surfaces[view->apps.apps[app].windows[index]] == top)
				view->current = (int)app;
		}
	}

	/* Succeeded: there are applications. */
	return 1;
}

/* Tells which slot of the bar is under a point: an application's index, SLOT_MORE, or SLOT_NONE. */
static int
slot_at(
	const struct apps_view *view,
	int32_t x,
	int32_t y)
{
	int32_t slot;

	/* Only in the bar, in the icons' span. */
	if (y < 0 ||
	    y >= ZWL_GLASS_BAR ||
	    x < view->left)
		return SLOT_NONE;
	slot = (x - view->left) / ICON_WIDTH;

	/* An application's icon, or the "+N" place after them. */
	if (slot < (int32_t)view->shown)
		return (int)slot;
	if (slot == (int32_t)view->shown && view->hidden > 0U)
		return SLOT_MORE;

	/* Succeeded: nothing there. */
	return SLOT_NONE;
}

/* Gives a slot's place in the bar. */
static void
slot_rect(
	const struct apps_view *view,
	unsigned slot,
	struct apps_rect *rect)
{
	/* One after the other from the left of the span, the bar's height. */
	rect->x = view->left + (int32_t)slot * ICON_WIDTH;
	rect->y = 0;
	rect->width = ICON_WIDTH;
	rect->height = ZWL_GLASS_BAR;
}

/*
 * Lays out the previews of an application under its icon, for the switcher (switcher-shell.c).  Returns 0 when it has no icon.
 */
int
zwl_apps_bar_panel(
	struct zwl_server *server,
	const struct apps_view *view,
	const char *key,
	struct apps_panel *panel)
{
	int built;

	/* As the bar's own. */
	built = panel_build(server, view, key, panel);
	if (!built)
		return 0;

	/* Succeeded: laid out. */
	return 1;
}

/* Lays out the previews of an application under its icon (zwl_apps_tiles_layout).  Returns 0 when the application has no icon. */
static int
panel_build(
	struct zwl_server *server,
	const struct apps_view *view,
	const char *key,
	struct apps_panel *panel)
{
	struct apps_rect icon;
	int32_t center;
	unsigned index;
	int found;

	/* The application, among those with an icon. */
	found = zwl_apps_find(&view->apps, key);
	if (found < 0 || found >= (int)view->shown)
		return 0;
	zwl_apps_tiles_layout(server, view, (unsigned)found, panel);

	/* The panel under the icon, inside the output. */
	slot_rect(view, (unsigned)found, &icon);
	center = icon.x + icon.width / 2;
	panel->rect.width += 2 * PANEL_PAD;
	panel->rect.height += PANEL_PAD;
	panel->rect.x = center - panel->rect.width / 2;
	if (panel->rect.x + panel->rect.width > (int32_t)server->width - 16)
		panel->rect.x = (int32_t)server->width - 16 - panel->rect.width;
	if (panel->rect.x < 16)
		panel->rect.x = 16;
	panel->rect.y = ZWL_GLASS_BAR + PANEL_DROP;

	/* The previews moved into the panel. */
	for (index = 0; index < panel->count; index++) {
		panel->tiles[index].x += panel->rect.x + PANEL_PAD;
		panel->tiles[index].y += panel->rect.y + PANEL_PAD;
	}

	/* Succeeded: laid out. */
	return 1;
}

/*
 * Lays out an application's windows as previews from 0, 0: each at most a
 * quarter of the output's width and height (D4), keeping its shape; all
 * of them smaller together (to half) when one row is wider than the
 * output, then in more rows; under each row the room for the previews'
 * labels.  The panel's rectangle gets the size they take (no padding, no
 * place).
 */
void
zwl_apps_tiles_layout(
	struct zwl_server *server,
	const struct apps_view *view,
	unsigned found,
	struct apps_panel *panel)
{
	const struct zwl_app *app;
	uint32_t width;
	uint32_t height;
	int32_t sizes[ZWL_APPS_WINDOWS][2];
	int32_t available;
	int32_t total;
	int32_t row_x;
	int32_t row_y;
	int32_t row_height;
	int32_t widest;
	float scale;
	float shrink;
	unsigned index;

	/* The application and its windows. */
	app = &view->apps.apps[found];
	panel->app = (int)found;
	panel->count = app->window_count;

	/* Each window's size at most a quarter of the output's width and height. */
	total = 0;
	for (index = 0; index < panel->count; index++) {
		panel->surfaces[index] = view->surfaces[app->windows[index]];
		zwl_surface_size(panel->surfaces[index], &width, &height);
		if (width == 0U || height == 0U) {
			width = 640U;
			height = 400U;
		}

		/* The quarter's scale, never larger than the window. */
		scale = (float)server->width / 4.0f / (float)width;
		if ((float)server->height / 4.0f / (float)height < scale)
			scale = (float)server->height / 4.0f / (float)height;
		if (scale > 1.0f)
			scale = 1.0f;
		sizes[index][0] = (int32_t)((float)width * scale);
		sizes[index][1] = (int32_t)((float)height * scale);
		total += sizes[index][0] + PREVIEW_GAP;
	}

	/* All smaller together when one row is wider than the room, to half at most. */
	available = (int32_t)server->width - 32 - 2 * PANEL_PAD;
	total -= PREVIEW_GAP;
	if (total > available) {
		shrink = (float)available / (float)total;
		if (shrink < 0.5f)
			shrink = 0.5f;
		for (index = 0; index < panel->count; index++) {
			sizes[index][0] = (int32_t)((float)sizes[index][0] * shrink);
			sizes[index][1] = (int32_t)((float)sizes[index][1] * shrink);
		}
	}

	/* Left to right, a new row when the room runs out. */
	row_x = 0;
	row_y = 0;
	row_height = 0;
	widest = 0;
	for (index = 0; index < panel->count; index++) {
		if (row_x > 0 && row_x + sizes[index][0] > available) {
			row_y += row_height + PREVIEW_LABEL;
			row_x = 0;
			row_height = 0;
		}

		/* The preview in its row. */
		panel->tiles[index].x = row_x;
		panel->tiles[index].y = row_y;
		panel->tiles[index].width = sizes[index][0];
		panel->tiles[index].height = sizes[index][1];
		row_x += sizes[index][0] + PREVIEW_GAP;
		if (row_x - PREVIEW_GAP > widest)
			widest = row_x - PREVIEW_GAP;
		if (sizes[index][1] > row_height)
			row_height = sizes[index][1];
	}

	/* The size they take, with the last row's labels. */
	panel->rect.x = 0;
	panel->rect.y = 0;
	panel->rect.width = widest;
	panel->rect.height = row_y + row_height + PREVIEW_LABEL;
}

/*
 * Tells which preview is under a point, or -1.
 */
int
zwl_apps_tile_at(
	const struct apps_rect *tiles,
	unsigned count,
	int32_t x,
	int32_t y)
{
	unsigned index;
	int hit;

	/* Each preview. */
	for (index = 0; index < count; index++) {
		hit = zwl_apps_inside(&tiles[index], x, y);
		if (hit)
			return (int)index;
	}

	/* Succeeded: none. */
	return -1;
}

/*
 * Tells whether a point is inside a rectangle.
 */
int
zwl_apps_inside(
	const struct apps_rect *rect,
	int32_t x,
	int32_t y)
{
	/* Within both ranges. */
	if (x < rect->x || x >= rect->x + rect->width)
		return 0;
	if (y < rect->y || y >= rect->y + rect->height)
		return 0;
	return 1;
}

/*
 * Shows an application's previews, and says where they are.
 */
void
zwl_apps_bar_show(
	struct zwl_server *server,
	const struct apps_view *view,
	const char *key,
	unsigned via)
{
	struct zwl_apps_bar *state;
	struct apps_panel panel;
	const char *how;
	unsigned index;
	int built;

	/* The application's panel, if it has an icon. */
	state = &server->apps_bar;
	built = panel_build(server, view, key, &panel);
	if (!built) {
		zwl_apps_bar_hide(server, "gone");
		return;
	}

	/* Shown. */
	state->state = ZWL_APPS_SHOWN;
	state->via = via;
	state->left = 0;
	(void)snprintf(state->key, sizeof(state->key), "%s", key);
	server->dirty = 1;

	/* Logged with each preview's place. */
	how = "hover";
	if (via == ZWL_APPS_VIA_CLICK)
		how = "click";
	if (via == ZWL_APPS_VIA_SWITCH)
		how = "switch";
	printf("ZWL APPS preview app=%s windows=%u via=%s at_ms=%llu\n", key, panel.count, how, (unsigned long long)zwl_milliseconds());
	for (index = 0; index < panel.count; index++)
		printf("ZWL APPS preview window surface=%u x=%d y=%d width=%d height=%d client=%llu\n", panel.surfaces[index]->id, panel.tiles[index].x, panel.tiles[index].y, panel.tiles[index].width, panel.tiles[index].height, (unsigned long long)panel.surfaces[index]->client->number);
}

/*
 * Hides the previews (or stops the wait for them).
 */
void
zwl_apps_bar_hide(
	struct zwl_server *server,
	const char *why)
{
	struct zwl_apps_bar *state;

	/* Logged only when they showed. */
	state = &server->apps_bar;
	if (state->state == ZWL_APPS_SHOWN)
		printf("ZWL APPS preview close via=%s\n", why);

	/* Idle. */
	state->state = ZWL_APPS_IDLE;
	state->key[0] = '\0';
	state->left = 0;
	server->dirty = 1;
}

/* Logs the bar's applications and their icons' places when they changed. */
static void
log_bar(
	struct zwl_server *server,
	const struct apps_view *view)
{
	struct zwl_apps_bar *state;
	struct apps_rect rect;
	char line[sizeof(server->apps_bar.logged)];
	const char *separator;
	size_t used;
	unsigned index;
	int same;
	int length;

	/* The line: the count, and the applications from the left. */
	state = &server->apps_bar;
	length = snprintf(line, sizeof(line), "count=%u hidden=%u desktop=%u apps=", view->apps.count, view->hidden, server->desktop + 1U);
	used = 0;
	if (length > 0)
		used = (size_t)length;
	for (index = 0; index < view->shown && used < sizeof(line); index++) {
		separator = ",";
		if (index == 0U)
			separator = "";
		length = snprintf(line + used, sizeof(line) - used, "%s%s", separator, view->apps.apps[index].key);
		if (length > 0)
			used += (size_t)length;
	}

	/* Unchanged: nothing to say. */
	same = strcmp(line, state->logged);
	if (same == 0)
		return;
	(void)snprintf(state->logged, sizeof(state->logged), "%s", line);

	/* The bar, then each icon. */
	printf("ZWL APPS bar %s\n", line);
	for (index = 0; index < view->shown; index++) {
		slot_rect(view, index, &rect);
		printf("ZWL APPS icon app=%s x=%d y=%d width=%d height=%d windows=%u\n", view->apps.apps[index].key, rect.x, rect.y, rect.width, rect.height, view->apps.apps[index].window_count);
	}
}

/* Draws the "+N" place: how many applications have no room. */
static void
draw_more(
	struct zwl_server *server,
	VkCommandBuffer command,
	const struct apps_rect *rect,
	unsigned hidden,
	float light)
{
	static const float pill[4] = { 1.0f, 1.0f, 1.0f, 0.18f };
	static const float ink[4] = { 1.0f, 1.0f, 1.0f, 0.94f };
	char text[16];
	int32_t width;

	/* A light rounded square the shape of the tiles, lit while pressed. */
	if (light > 0.0f)
		draw_light(server, command, rect, light);
	glass_draw_solid(server, command, (float)(rect->x + (ICON_WIDTH - ICON_MARK) / 2), (float)(ZWL_GLASS_BAR / 2 - ICON_MARK / 2), (float)ICON_MARK, (float)ICON_MARK, (float)ICON_MARK * GLASS_ICON_TILE_RADIUS, pill);

	/* The count in its middle. */
	(void)snprintf(text, sizeof(text), "+%u", hidden);
	width = glass_text_width(server, SIZE_BAR, text);
	glass_draw_text(server, command, SIZE_BAR, rect->x + ICON_WIDTH / 2 - width / 2, ZWL_GLASS_BAR / 2 + 5, text, ICON_WIDTH, ink);
}

/* Draws the light behind an icon the pointer rests on, or whose previews show. */
static void
draw_light(
	struct zwl_server *server,
	VkCommandBuffer command,
	const struct apps_rect *rect,
	float strength)
{
	float colour[4];

	/* A soft light rounded square behind the mark, the shape of the tiles, inside the pill. */
	colour[0] = 1.0f;
	colour[1] = 1.0f;
	colour[2] = 1.0f;
	colour[3] = 0.20f * strength;
	glass_draw_solid(server, command, (float)(rect->x + 2), (float)(ZWL_GLASS_BAR / 2 - ICON_WIDTH / 2 + 2), (float)(ICON_WIDTH - 4), (float)(ICON_WIDTH - 4), (float)(ICON_WIDTH - 4) * GLASS_ICON_TILE_RADIUS, colour);
}
