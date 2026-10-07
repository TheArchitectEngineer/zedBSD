/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arrangement menu and the arrangement mode of the glass look (WS181,
 * the 2026-10-07 UAT).
 *
 * A tap or a click anywhere on the system bar's desktops' pill opens the
 * menu under it (the 2026-10-07 user decision D5): the seven layouts'
 * drawings, without words, two to a row in a large menu (the 2026-10-07
 * UAT, ws181-p006: an important choice should look it): side by side and
 * stacked; one on the left and one on the right; one on the top and one on
 * the bottom; the grid alone on the last row.  The menu acts on the desktop
 * shown; a desktop is switched by the swipe and the keys, not from the
 * pill.  Choosing a layout arranges the windows of the desktop shown --
 * up to the layout's limit, top first, the rest staying where they are --
 * each into the slot nearest to where it was (arrange.c), floating (the
 * docked mode ends quietly first), and they glide there.
 *
 * The desktop is then in the arrangement mode: a drag of an arranged
 * window's title swaps it with the window of the slot it is let go on (a
 * drag into the system bar docks it); a double click on a title docks it.
 * Docking a window, closing or minimizing an arranged one, a new window on
 * that desktop, or an arranged window resized or gone fullscreen ends the
 * mode, and the windows are plain floating windows where they are -- there
 * is nothing to undo (the user's "no steps to learn to leave it").
 *
 * The state is this file's: the menu, each desktop's arrangement and the
 * swap being dragged.  The windows' places are set through shell.c's
 * kwl_glass_* functions.
 */

#include "glass.h"
#include "arrange.h"
#include "desktop.h"
#include "extras.h"
#include "menu.h"

#include <keiland/keiland.h>

#include <stdio.h>
#include <string.h>

/* Marks a parameter a function does not use. */
#define UNUSED_PARAMETER(name)	((void)(name))

/* How long a window glides to its slot, in milliseconds. */
#define ARRANGE_MS		180U

/*
 * The menu's cells (one a layout) in two columns and four rows, the gap
 * between them, its padding, its corner, and its gap under the bar: twice
 * as wide and five times as tall as the row of ws181-p005.
 */
#define ARRANGE_MENU_COLUMNS		2
#define ARRANGE_MENU_ROWS		4
#define ARRANGE_MENU_CELL_WIDTH		258
#define ARRANGE_MENU_CELL_HEIGHT	56
#define ARRANGE_MENU_CELL_GAP		8
#define ARRANGE_MENU_PAD		6
#define ARRANGE_MENU_RADIUS		18.0f
#define ARRANGE_MENU_GAP		6
#define ARRANGE_MENU_WIDTH		(2 * ARRANGE_MENU_PAD + ARRANGE_MENU_COLUMNS * ARRANGE_MENU_CELL_WIDTH + (ARRANGE_MENU_COLUMNS - 1) * ARRANGE_MENU_CELL_GAP)
#define ARRANGE_MENU_HEIGHT		(2 * ARRANGE_MENU_PAD + ARRANGE_MENU_ROWS * ARRANGE_MENU_CELL_HEIGHT + (ARRANGE_MENU_ROWS - 1) * ARRANGE_MENU_CELL_GAP)

/* The drawing of a layout in its cell (its slots scaled down from a five times larger area). */
#define ARRANGE_ICON_WIDTH	60
#define ARRANGE_ICON_HEIGHT	40
#define ARRANGE_ICON_SCALE	5

/* The keys the open menu takes. */
#define ARRANGE_KEY_ESC		1U
#define ARRANGE_KEY_ENTER	28U
#define ARRANGE_KEY_KP_ENTER	96U
#define ARRANGE_KEY_UP		103U
#define ARRANGE_KEY_LEFT	105U
#define ARRANGE_KEY_RIGHT	106U
#define ARRANGE_KEY_DOWN	108U

/* The menu's items: the layouts, in their order. */
#define ARRANGE_ITEMS		KWL_ARRANGE_LAYOUTS
#define ARRANGE_ITEM_NONE	(-1)

/*
 * One slot of an arrangement: its rectangle (the window's whole frame), the
 * window in it (NULL once it went), and the glide of the window into it
 * (the body's rectangle it started from and goes to, and when it started).
 */
struct arrange_slot {
	struct kwl_arrange_rect slot;
	struct kwl_object *window;
	int32_t from[4];
	int32_t to[4];
	uint64_t glide_ms;
};

/*
 * A desktop's arrangement: whether it is in the arrangement mode, its
 * layout, its slots, a window of it that went (kwl_arrange_forget, ended at
 * the next tick), and the newest window there when it was arranged (a
 * window mapped later ends the mode).
 */
struct arrange_desktop {
	unsigned on;
	unsigned layout;
	unsigned count;
	unsigned gone;
	uint64_t map_order;
	struct arrange_slot slots[KWL_ARRANGE_MAX];
};

/*
 * The menu: whether it is open, where, the item a press is on (its release
 * acts on it), the item the keyboard selected, and whether a press on the
 * pill waits for its release to open the menu.
 */
struct arrange_menu {
	unsigned open;
	int32_t x;
	int32_t y;
	int32_t height;
	int pressed;
	int selected;
	unsigned pill_pressed;
	unsigned swallow;
};

/*
 * The swap being dragged: the arranged window following the pointer, its
 * slot, its desktop, and where the pointer holds it.
 */
struct arrange_swap {
	struct kwl_object *window;
	unsigned slot;
	unsigned desktop;
	int32_t dx;
	int32_t dy;
};

/*
 * Each desktop's arrangement, for the session.  A desktop out of the
 * arrangement mode has on 0; its slots are forgotten then.
 */
static struct arrange_desktop arrange_desktops[KWL_APPS_DESKTOPS];

/* The menu, closed at the start; only one is open at a time. */
static struct arrange_menu arrange_menu;

/* The swap being dragged; window is NULL when none is. */
static struct arrange_swap arrange_swap;

static void arrange_menu_open(struct kwl_server *server);
static void arrange_menu_close(struct kwl_server *server, const char *via);
static int arrange_menu_item_at(struct kwl_server *server, int32_t x, int32_t y);
static void arrange_menu_item_rect(int item, int32_t *x, int32_t *y, int32_t *width, int32_t *height);
static void arrange_menu_act(struct kwl_server *server, int item);
static void arrange_apply(struct kwl_server *server, unsigned layout);
static unsigned arrange_targets(struct kwl_server *server, struct kwl_object **windows, unsigned limit);
static void arrange_body(const struct kwl_object *surface, const struct kwl_arrange_rect *slot, int32_t body[4]);
static void arrange_glide_to(struct kwl_server *server, struct arrange_slot *slot, unsigned index);
static void arrange_end(struct kwl_server *server, unsigned desktop, const char *reason);
static int arrange_slot_of(unsigned desktop, const struct kwl_object *surface);
static void arrange_draw_icon(struct kwl_server *server, VkCommandBuffer command, unsigned layout, int32_t x, int32_t y, const float *ink, float alpha);
static void arrange_swap_end(struct kwl_server *server);

/*
 * Handles a pointer button for the arrangement menu: a release of a press
 * on the desktops' pill opens or closes it (the menu opens on the release,
 * as from the top band's tap); while it is open, a release on the item the
 * press was on acts on it, and a press outside closes it.  Returns 1 when
 * the button is the menu's.
 */
int
kwl_arrange_button(
	struct kwl_server *server,
	uint32_t button,
	uint32_t state)
{
	int32_t pill_x;
	int32_t pill_width;
	int on_pill;
	int item;

	/* Whether the pointer is on the desktops' pill (shell.c). */
	kwl_glass_desktops_pill(server, &pill_x, &pill_width);
	on_pill = 0;
	if (server->pointer_y < KWL_GLASS_BAR &&
	    server->pointer_x >= pill_x &&
	    server->pointer_x < pill_x + pill_width)
		on_pill = 1;

	/* The release of a press on the pill toggles the menu. */
	if (state == 0 && arrange_menu.pill_pressed) {
		arrange_menu.pill_pressed = 0;
		if (arrange_menu.open) {
			arrange_menu_close(server, "pill");
		} else {
			arrange_menu_open(server);
		}
		return 1;
	}

	/* The release of a press that closed the menu goes no further. */
	if (state == 0 && arrange_menu.swallow) {
		arrange_menu.swallow = 0;
		return 1;
	}

	/* With the menu closed, only a left press on the pill, held until its release. */
	if (!arrange_menu.open) {
		if (state == 0 ||
		    button != KWL_BUTTON_LEFT ||
		    !on_pill)
			return 0;
		arrange_menu.pill_pressed = 1;
		return 1;
	}

	/* While it is open, a release acts on the item its press was on, when it is still there. */
	if (state == 0) {
		item = arrange_menu_item_at(server, server->pointer_x, server->pointer_y);
		if (arrange_menu.pressed != ARRANGE_ITEM_NONE && item == arrange_menu.pressed)
			arrange_menu_act(server, item);
		arrange_menu.pressed = ARRANGE_ITEM_NONE;
		return 1;
	}

	/* A press on the pill closes it at its release. */
	if (on_pill) {
		arrange_menu.pill_pressed = 1;
		return 1;
	}

	/* A press outside the menu closes it and goes no further. */
	if (server->pointer_x < arrange_menu.x ||
	    server->pointer_x >= arrange_menu.x + ARRANGE_MENU_WIDTH ||
	    server->pointer_y < arrange_menu.y ||
	    server->pointer_y >= arrange_menu.y + arrange_menu.height) {
		arrange_menu_close(server, "outside");
		arrange_menu.swallow = 1;
		return 1;
	}

	/* A left press on an item waits for its release. */
	arrange_menu.pressed = ARRANGE_ITEM_NONE;
	if (button == KWL_BUTTON_LEFT)
		arrange_menu.pressed = arrange_menu_item_at(server, server->pointer_x, server->pointer_y);

	/* Succeeded: the press was the menu's. */
	return 1;
}

/*
 * Handles a key while the menu is open: Left and Right move the selection
 * along the cells, Up and Down a row, Enter acts on it, Esc closes the
 * menu.  Returns 1 when the key is the
 * menu's (every key while it is open).
 */
int
kwl_arrange_key(
	struct kwl_server *server,
	uint32_t key,
	uint32_t state)
{
	/* A closed menu takes no key. */
	if (!arrange_menu.open)
		return 0;

	/* Only presses act; releases are the menu's too. */
	if (state == 0)
		return 1;

	/* Each key's work. */
	switch (key) {
	case ARRANGE_KEY_ESC:
		arrange_menu_close(server, "key");
		break;
	case ARRANGE_KEY_LEFT:
		arrange_menu.selected--;
		if (arrange_menu.selected < 0)
			arrange_menu.selected = (int)ARRANGE_ITEMS - 1;
		server->dirty = 1;
		break;
	case ARRANGE_KEY_RIGHT:
		arrange_menu.selected++;
		if (arrange_menu.selected >= (int)ARRANGE_ITEMS)
			arrange_menu.selected = 0;
		server->dirty = 1;
		break;
	case ARRANGE_KEY_UP:
		/* A row up; the first row stays. */
		if (arrange_menu.selected >= ARRANGE_MENU_COLUMNS)
			arrange_menu.selected -= ARRANGE_MENU_COLUMNS;
		server->dirty = 1;
		break;
	case ARRANGE_KEY_DOWN:
		/* A row down; the last row (the grid alone) takes either cell above it. */
		arrange_menu.selected += ARRANGE_MENU_COLUMNS;
		if (arrange_menu.selected >= (int)ARRANGE_ITEMS)
			arrange_menu.selected = (int)ARRANGE_ITEMS - 1;
		server->dirty = 1;
		break;
	case ARRANGE_KEY_ENTER:
	case ARRANGE_KEY_KP_ENTER:
		arrange_menu_act(server, arrange_menu.selected);
		break;
	default:
		break;
	}

	/* Succeeded: the key was the menu's. */
	return 1;
}

/*
 * Follows the pointer for the menu (the item under it is lit) and for a
 * swap being dragged (the window follows the pointer).  Returns 1 when the
 * motion is the arrangement's.
 */
int
kwl_arrange_motion(
	struct kwl_server *server)
{
	struct kwl_object *surface;

	/* An open menu lights the item under the pointer. */
	if (arrange_menu.open) {
		server->dirty = 1;
		return 1;
	}

	/* Only a swap being dragged follows the pointer. */
	surface = arrange_swap.window;
	if (surface == NULL)
		return 0;

	/* The window follows the pointer, its title below the system bar. */
	surface->x = server->pointer_x - arrange_swap.dx;
	surface->y = server->pointer_y - arrange_swap.dy;
	if (surface->y < KWL_GLASS_BAR + KWL_GLASS_GAP + KWL_GLASS_TITLE)
		surface->y = KWL_GLASS_BAR + KWL_GLASS_GAP + KWL_GLASS_TITLE;
	server->dirty = 1;

	/* Succeeded: the motion is the swap's. */
	return 1;
}

/*
 * Starts a move of a window from one of its four ways (a press on its
 * title, a press on its title's menu or field that moved, the client's
 * own move request, a finger on its title): an arranged window's move is a
 * swap instead, which follows the pointer until it is let go.  Returns 1
 * when the swap started (the caller starts no move).
 */
int
kwl_arrange_move_start(
	struct kwl_server *server,
	struct kwl_object *surface,
	int32_t x,
	int32_t y)
{
	int slot;

	UNUSED_PARAMETER(server);

	/* Only an arranged window of the desktop shown, and no swap already. */
	if (arrange_swap.window != NULL)
		return 0;
	if (surface->desktop >= KWL_APPS_DESKTOPS || !arrange_desktops[surface->desktop].on)
		return 0;
	slot = arrange_slot_of(surface->desktop, surface);
	if (slot < 0)
		return 0;

	/* The swap holds the window where it was pressed. */
	arrange_swap.window = surface;
	arrange_swap.slot = (unsigned)slot;
	arrange_swap.desktop = surface->desktop;
	arrange_swap.dx = x - surface->x;
	arrange_swap.dy = y - surface->y;
	arrange_desktops[surface->desktop].slots[slot].glide_ms = 0U;
	printf("KWL ARRANGE swap-start surface=%u slot=%d\n", surface->id, slot);

	/* Succeeded: the move is a swap. */
	return 1;
}

/*
 * Ends a swap at the button's release: let go on another slot, the two
 * windows trade slots; let go in the system bar, the window docks (which
 * ends the arrangement mode); anywhere else, it glides back to its slot.
 * Returns 1 when a swap ended (the release was the arrangement's).
 */
int
kwl_arrange_move_end(
	struct kwl_server *server)
{
	struct arrange_desktop *arranged;
	struct kwl_object *surface;
	struct kwl_object *other;
	unsigned index;
	unsigned target;
	int found;

	/* Only a swap being dragged. */
	surface = arrange_swap.window;
	if (surface == NULL)
		return 0;
	arrange_swap.window = NULL;
	arranged = &arrange_desktops[arrange_swap.desktop];

	/* Let go in the system bar: docked, which ends the mode (kwl_arrange_end_all). */
	if (server->pointer_y < KWL_GLASS_BAR) {
		kwl_glass_dock_window(server, surface, "arrange-drag");
		return 1;
	}

	/* The slot the pointer is in, other than the window's own. */
	found = 0;
	target = 0U;
	for (index = 0U; index < arranged->count; index++) {
		if (index == arrange_swap.slot)
			continue;
		if (server->pointer_x >= arranged->slots[index].slot.x &&
		    server->pointer_x < arranged->slots[index].slot.x + arranged->slots[index].slot.width &&
		    server->pointer_y >= arranged->slots[index].slot.y &&
		    server->pointer_y < arranged->slots[index].slot.y + arranged->slots[index].slot.height) {
			found = 1;
			target = index;
			break;
		}
	}

	/* Nowhere else: back to its own slot. */
	if (!found) {
		arrange_glide_to(server, &arranged->slots[arrange_swap.slot], arrange_swap.slot);
		printf("KWL ARRANGE swap-back surface=%u slot=%u\n", surface->id, arrange_swap.slot);
		return 1;
	}

	/* The two windows trade slots and glide there. */
	other = arranged->slots[target].window;
	arranged->slots[target].window = surface;
	arranged->slots[arrange_swap.slot].window = other;
	arrange_glide_to(server, &arranged->slots[target], target);
	if (other != NULL)
		arrange_glide_to(server, &arranged->slots[arrange_swap.slot], arrange_swap.slot);
	printf("KWL ARRANGE swap a=%u b=%u slots=%u,%u\n", surface->id, other != NULL ? other->id : 0U, arrange_swap.slot, target);

	/* Succeeded: the swap is done. */
	return 1;
}

/*
 * Gives the body of an arranged window gliding to its slot, while it
 * glides.  Returns 1 and sets body (x, y, width, height) when it does.
 */
int
kwl_arrange_glide(
	struct kwl_server *server,
	const struct kwl_object *surface,
	int32_t body[4])
{
	struct arrange_slot *slot;
	uint64_t elapsed;
	unsigned part;
	float t;
	int index;

	/* Only an arranged window. */
	if (surface->desktop >= KWL_APPS_DESKTOPS || !arrange_desktops[surface->desktop].on)
		return 0;
	index = arrange_slot_of(surface->desktop, surface);
	if (index < 0)
		return 0;
	slot = &arrange_desktops[surface->desktop].slots[index];

	/* Only while it glides. */
	if (slot->glide_ms == 0U)
		return 0;

	/* A glide that has run its time is over: the window is drawn in its slot from now on (the tests wait for this line). */
	elapsed = kwl_milliseconds() - slot->glide_ms;
	if (elapsed >= ARRANGE_MS) {
		slot->glide_ms = 0U;
		printf("KWL ARRANGE glide-end surface=%u slot=%d\n", surface->id, index);
		return 0;
	}

	/* Eased out (1 - (1 - t)^3) from where it was to its slot. */
	t = (float)elapsed / (float)ARRANGE_MS;
	t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
	for (part = 0U; part < 4U; part++)
		body[part] = slot->from[part] + (int32_t)((float)(slot->to[part] - slot->from[part]) * t);
	server->dirty = 1;

	/* Succeeded: the window glides. */
	return 1;
}

/*
 * Follows the arrangements every frame: a desktop whose arranged window
 * went (destroyed, unmapped, minimized, fullscreen, on another desktop, or
 * resized by its frame) leaves the arrangement mode, its windows floating
 * where they are.
 */
void
kwl_arrange_tick(
	struct kwl_server *server)
{
	struct arrange_desktop *arranged;
	struct kwl_object *surface;
	int32_t body[4];
	unsigned desktop;
	unsigned index;
	const char *reason;

	/* Each desktop in the arrangement mode. */
	for (desktop = 0U; desktop < KWL_APPS_DESKTOPS; desktop++) {
		arranged = &arrange_desktops[desktop];
		if (!arranged->on)
			continue;

		/* A window that went ends it. */
		reason = NULL;
		if (arranged->gone)
			reason = "closed";

		/* Each arranged window, as it is now. */
		for (index = 0U; index < arranged->count; index++) {
			/* The first reason found is enough. */
			surface = arranged->slots[index].window;
			if (reason != NULL)
				break;
			if (surface == NULL)
				continue;
			if (surface->dead || !surface->mapped) {
				reason = "closed";
			} else if (surface->minimized) {
				reason = "minimized";
			} else if (surface->fullscreen) {
				reason = "fullscreen";
			} else if (surface->maximized) {
				reason = "dock";
			} else if (surface->desktop != desktop) {
				reason = "moved";
			} else if (arrange_swap.window != surface && arranged->slots[index].glide_ms == 0U) {
				/* Not swapped nor gliding: its size is its slot's unless its frame resized it. */
				arrange_body(surface, &arranged->slots[index].slot, body);
				if ((int32_t)surface->window_width != body[2] || (int32_t)surface->window_height != body[3])
					reason = "resized";
			}
		}

		/* The mode ends for the first reason found. */
		if (reason != NULL)
			arrange_end(server, desktop, reason);
	}
}

/*
 * Forgets a destroyed window (objects.c through kwl_glass_forget): its slot
 * is emptied and its desktop's arrangement ends at the next tick; a swap
 * of it ends.
 */
void
kwl_arrange_forget(
	struct kwl_server *server,
	struct kwl_object *surface)
{
	unsigned desktop;
	int index;

	UNUSED_PARAMETER(server);

	/* A swap of it is over. */
	if (arrange_swap.window == surface)
		arrange_swap.window = NULL;

	/* Its slot, on whichever desktop. */
	for (desktop = 0U; desktop < KWL_APPS_DESKTOPS; desktop++) {
		index = arrange_slot_of(desktop, surface);
		if (index < 0)
			continue;
		arrange_desktops[desktop].slots[index].window = NULL;
		arrange_desktops[desktop].gone = 1U;
	}
}

/*
 * Ends the arrangement mode of every desktop (the docked mode starting:
 * shell.c's layout_set, WS181 I3), the menu and a swap with it.
 */
void
kwl_arrange_end_all(
	struct kwl_server *server,
	const char *reason)
{
	unsigned desktop;

	/* The menu and a swap. */
	arrange_swap.window = NULL;
	if (arrange_menu.open)
		arrange_menu_close(server, reason);

	/* Every arranged desktop. */
	for (desktop = 0U; desktop < KWL_APPS_DESKTOPS; desktop++) {
		if (arrange_desktops[desktop].on)
			arrange_end(server, desktop, reason);
	}
}

/*
 * Ends the arrangement mode of a desktop where a new window was mapped (a
 * window without a parent, newer than the arrangement; WS181 S6).
 */
void
kwl_arrange_mapped(
	struct kwl_server *server,
	struct kwl_object *surface)
{
	struct arrange_desktop *arranged;

	/* Only a window without a parent on an arranged desktop, newer than its arrangement. */
	if (surface->parent_window != NULL || surface->desktop >= KWL_APPS_DESKTOPS)
		return;
	arranged = &arrange_desktops[surface->desktop];
	if (!arranged->on || surface->map_order <= arranged->map_order)
		return;

	/* The mode ends; the new window is placed as a new window is. */
	arrange_end(server, surface->desktop, "new");
}

/*
 * Ends the arrangement mode of a desktop a window comes to from another
 * one, and of the desktop it leaves when it was arranged there.
 */
void
kwl_arrange_moved(
	struct kwl_server *server,
	struct kwl_object *surface,
	unsigned from)
{
	int index;

	/* The desktop it left, when it was arranged there. */
	if (from < KWL_APPS_DESKTOPS && arrange_desktops[from].on) {
		index = arrange_slot_of(from, surface);
		if (index >= 0)
			arrange_end(server, from, "moved");
	}

	/* The desktop it came to. */
	if (surface->desktop < KWL_APPS_DESKTOPS && arrange_desktops[surface->desktop].on)
		arrange_end(server, surface->desktop, "new");
}

/*
 * Draws the arrangement menu, when it is open, under the desktops' pill:
 * frosted glass with the seven layouts' drawings in their cells, the one
 * under the pointer or selected by the keyboard lit in the accent.
 */
void
kwl_arrange_draw(
	struct kwl_server *server,
	VkCommandBuffer command)
{
	static const float dark[4] = { 0.12f, 0.16f, 0.24f, 1.0f };
	static const float soft[4] = { 0.40f, 0.46f, 0.56f, 1.0f };
	struct glass_shape shape;
	struct kwl_object *windows[KWL_ARRANGE_MAX];
	float accent[4];
	float accent_ink[4];
	const float *ink;
	unsigned kept;
	unsigned item;
	unsigned count;
	int over;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;

	/* Only an open menu. */
	if (!arrange_menu.open)
		return;

	/* The shadow and the frosted glass (the 2026-10-07 UAT), as the power dialog's card. */
	glass_shape_init(&shape, (float)arrange_menu.x, (float)arrange_menu.y + 6.0f, (float)ARRANGE_MENU_WIDTH, (float)arrange_menu.height);
	shape.quad[0] -= 40.0f;
	shape.quad[1] -= 40.0f;
	shape.quad[2] += 80.0f;
	shape.quad[3] += 80.0f;
	shape.mode = MODE_SHADOW;
	shape.radius = ARRANGE_MENU_RADIUS;
	shape.soft = 18.0f;
	shape.color[0] = 0.10f;
	shape.color[1] = 0.18f;
	shape.color[2] = 0.35f;
	shape.color[3] = 0.20f;
	glass_shape_draw(server, command, &shape);
	glass_shape_init(&shape, (float)arrange_menu.x, (float)arrange_menu.y, (float)ARRANGE_MENU_WIDTH, (float)arrange_menu.height);
	shape.mode = MODE_GLASS;
	shape.radius = ARRANGE_MENU_RADIUS;
	shape.soft = 1.0f;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = 0.62f;
	shape.edge = 0.70f;
	glass_shape_draw(server, command, &shape);

	/* The item under the pointer, or the keyboard's. */
	over = arrange_menu_item_at(server, server->pointer_x, server->pointer_y);
	if (over == ARRANGE_ITEM_NONE)
		over = arrange_menu.selected;

	/* How many windows the layouts would take (none: their drawings are faint). */
	count = arrange_targets(server, windows, KWL_ARRANGE_MAX);

	/* The accent and its ink, for the lit item. */
	kwl_accent_colour(server, server->dark, KWL_ACCENT_FILL, 1.0f, accent);
	kwl_accent_colour(server, server->dark, KWL_ACCENT_INK, 1.0f, accent_ink);

	/* Each layout's cell: its drawing in the middle. */
	for (item = 0U; item < ARRANGE_ITEMS; item++) {
		arrange_menu_item_rect((int)item, &x, &y, &width, &height);

		/* The lit cell is a rounded square of the accent with its ink, as they are. */
		ink = dark;
		kept = server->keep_colours;
		if ((int)item == over) {
			kept = kwl_accent_as_is(server);
			glass_draw_solid(server, command, (float)x, (float)y, (float)width, (float)height, 12.0f, accent);
			ink = accent_ink;
		}

		/* The drawing, faint with no window to arrange. */
		if (count == 0U && (int)item != over)
			ink = soft;
		arrange_draw_icon(server, command, item, x + (width - ARRANGE_ICON_WIDTH) / 2, y + (height - ARRANGE_ICON_HEIGHT) / 2, ink, 1.0f);
		kwl_accent_done(server, kept);
	}
}

/* Opens the menu under the desktops' pill, the keyboard's selection on the first layout; the log names each item's middle (the tests click them). */
static void
arrange_menu_open(
	struct kwl_server *server)
{
	int32_t pill_x;
	int32_t pill_width;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	unsigned item;

	/* Under the pill's middle, inside the output. */
	kwl_glass_desktops_pill(server, &pill_x, &pill_width);
	arrange_menu.x = pill_x + pill_width / 2 - ARRANGE_MENU_WIDTH / 2;
	if (arrange_menu.x < 8)
		arrange_menu.x = 8;
	if (arrange_menu.x + ARRANGE_MENU_WIDTH > (int32_t)server->width - 8)
		arrange_menu.x = (int32_t)server->width - 8 - ARRANGE_MENU_WIDTH;
	arrange_menu.y = KWL_GLASS_BAR + ARRANGE_MENU_GAP;
	arrange_menu.height = ARRANGE_MENU_HEIGHT;
	arrange_menu.open = 1;
	arrange_menu.pressed = ARRANGE_ITEM_NONE;
	arrange_menu.selected = 0;
	server->dirty = 1;

	/* The log: the menu, then each layout's middle. */
	printf("KWL ARRANGE menu open x=%d y=%d width=%d height=%d\n", arrange_menu.x, arrange_menu.y, ARRANGE_MENU_WIDTH, arrange_menu.height);
	for (item = 0U; item < ARRANGE_ITEMS; item++) {
		arrange_menu_item_rect((int)item, &x, &y, &width, &height);
		printf("KWL ARRANGE menu item=%s x=%d y=%d\n", kwl_arrange_name(item), x + width / 2, y + height / 2);
	}
}

/* Closes the menu. */
static void
arrange_menu_close(
	struct kwl_server *server,
	const char *via)
{
	/* Closed, nothing pressed. */
	arrange_menu.open = 0;
	arrange_menu.pressed = ARRANGE_ITEM_NONE;
	server->dirty = 1;
	printf("KWL ARRANGE menu close via=%s\n", via);
}

/* Gives the menu's item at a point, or ARRANGE_ITEM_NONE. */
static int
arrange_menu_item_at(
	struct kwl_server *server,
	int32_t x,
	int32_t y)
{
	unsigned item;
	int32_t left;
	int32_t top;
	int32_t width;
	int32_t height;

	UNUSED_PARAMETER(server);

	/* Each item's rectangle. */
	for (item = 0U; item < ARRANGE_ITEMS; item++) {
		arrange_menu_item_rect((int)item, &left, &top, &width, &height);
		if (x >= left && x < left + width && y >= top && y < top + height)
			return (int)item;
	}

	/* Succeeded: no item there. */
	return ARRANGE_ITEM_NONE;
}

/* Gives a layout's cell in the menu: two to a row, the grid alone across the last row. */
static void
arrange_menu_item_rect(
	int item,
	int32_t *x,
	int32_t *y,
	int32_t *width,
	int32_t *height)
{
	int column;
	int row;

	/* The cell's column and row, inside the padding. */
	column = item % ARRANGE_MENU_COLUMNS;
	row = item / ARRANGE_MENU_COLUMNS;
	*x = arrange_menu.x + ARRANGE_MENU_PAD + column * (ARRANGE_MENU_CELL_WIDTH + ARRANGE_MENU_CELL_GAP);
	*y = arrange_menu.y + ARRANGE_MENU_PAD + row * (ARRANGE_MENU_CELL_HEIGHT + ARRANGE_MENU_CELL_GAP);
	*width = ARRANGE_MENU_CELL_WIDTH;
	*height = ARRANGE_MENU_CELL_HEIGHT;

	/* The grid, alone on its row, takes the row's whole width. */
	if (item == (int)KWL_ARRANGE_GRID)
		*width = ARRANGE_MENU_COLUMNS * ARRANGE_MENU_CELL_WIDTH + (ARRANGE_MENU_COLUMNS - 1) * ARRANGE_MENU_CELL_GAP;
}

/* Acts on a menu's item: the layout arranges the windows of the desktop shown; the menu closes. */
static void
arrange_menu_act(
	struct kwl_server *server,
	int item)
{
	/* No item. */
	if (item == ARRANGE_ITEM_NONE)
		return;

	/* The menu closes first. */
	arrange_menu_close(server, "choice");

	/* Succeeded: the layout arranges the windows. */
	arrange_apply(server, (unsigned)item);
}

/*
 * Arranges the windows of the desktop shown in a layout (WS181 §4.4): the
 * docked mode ends quietly first (every window floating, no animation of
 * its own), each window goes to the slot nearest to where it floats
 * (arrange.c), told its size, and glides there from where it was drawn;
 * the desktop is in the arrangement mode.
 */
static void
arrange_apply(
	struct kwl_server *server,
	unsigned layout)
{
	struct kwl_object *windows[KWL_ARRANGE_MAX];
	struct kwl_arrange_rect area;
	struct kwl_arrange_rect slots[KWL_ARRANGE_MAX];
	struct arrange_desktop *arranged;
	int32_t from[KWL_ARRANGE_MAX][4];
	int32_t centres[KWL_ARRANGE_MAX][2];
	int32_t body[4];
	unsigned order[KWL_ARRANGE_MAX];
	unsigned count;
	unsigned made;
	unsigned index;
	unsigned slot;

	/* The windows to arrange, top first, up to the layout's limit. */
	count = arrange_targets(server, windows, kwl_arrange_limit(layout));
	if (count == 0U) {
		printf("KWL ARRANGE apply layout=%s desktop=%u windows=0\n", kwl_arrange_name(layout), server->desktop + 1U);
		return;
	}

	/* Where each is drawn now, the glides' start. */
	for (index = 0U; index < count; index++)
		kwl_glass_body(server, windows[index], from[index]);

	/* Every window floating, quietly (the docked mode ends without an animation). */
	kwl_glass_leave_quiet(server, "arrange");

	/* Each window's centre where it floats now (its body as drawn, the size its client drew). */
	for (index = 0U; index < count; index++) {
		kwl_glass_body(server, windows[index], body);
		centres[index][0] = body[0] + body[2] / 2;
		centres[index][1] = body[1] + body[3] / 2;
	}

	/* The slots in the work area, and which window goes to which. */
	kwl_glass_work_area(server, &area);
	made = kwl_arrange_slots(layout, count, &area, slots);
	kwl_arrange_assign(centres, made, slots, order);

	/* The desktop's arrangement. */
	arranged = &arrange_desktops[server->desktop];
	memset(arranged, 0, sizeof(*arranged));
	arranged->on = 1U;
	arranged->layout = layout;
	arranged->count = made;
	arranged->map_order = server->map_order;
	for (slot = 0U; slot < made; slot++)
		arranged->slots[slot].slot = slots[slot];

	/* Each window into its slot, gliding from where it was drawn, the arranged ones on top in the slots' order. */
	printf("KWL ARRANGE apply layout=%s desktop=%u windows=%u slots=", kwl_arrange_name(layout), server->desktop + 1U, made);
	for (index = made; index > 0U; index--) {
		slot = order[index - 1U];
		arranged->slots[slot].window = windows[index - 1U];
		memcpy(arranged->slots[slot].from, from[index - 1U], sizeof(arranged->slots[slot].from));
		kwl_glass_raise(server, windows[index - 1U]);
	}
	for (slot = 0U; slot < made; slot++) {
		arrange_body(arranged->slots[slot].window, &slots[slot], body);
		kwl_glass_place_body(server, arranged->slots[slot].window, body[0], body[1], body[2], body[3]);
		memcpy(arranged->slots[slot].to, body, sizeof(arranged->slots[slot].to));
		arranged->slots[slot].glide_ms = kwl_milliseconds();
		printf("%s%u@%d,%d,%d,%d", slot == 0U ? "" : ";", arranged->slots[slot].window->id, slots[slot].x, slots[slot].y, slots[slot].width, slots[slot].height);
	}
	printf("\n");
	server->dirty = 1;
}

/*
 * Finds the windows to arrange on the desktop shown, top first, up to a
 * limit: mapped, not minimized, not fullscreen, without a parent, not the
 * desktop's icons (in the docked mode the windows hidden by docking too).
 * Returns how many.
 */
static unsigned
arrange_targets(
	struct kwl_server *server,
	struct kwl_object **windows,
	unsigned limit)
{
	struct kwl_client *client;
	struct kwl_object *surface;
	unsigned count;
	unsigned index;
	unsigned place;
	int desktop_surface;

	/* Each window of the desktop shown, kept in order from the top. */
	count = 0U;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			/* Only live mapped windows of the desktop shown, without a parent, shown. */
			if (surface->kind != KWL_SURFACE ||
			    surface->dead ||
			    !surface->mapped ||
			    surface->role == NULL ||
			    surface->cursor_role ||
			    surface->parent_window != NULL ||
			    surface->desktop != server->desktop ||
			    surface->minimized ||
			    surface->fullscreen)
				continue;
			desktop_surface = kwl_desktop_is(surface);
			if (desktop_surface)
				continue;

			/* Its place in the order from the top. */
			place = count;
			while (place > 0U && windows[place - 1U]->map_order < surface->map_order)
				place--;
			if (place >= limit)
				continue;

			/* Kept there, the ones below moved down (the last past the limit is dropped). */
			if (count < limit)
				count++;
			for (index = count - 1U; index > place; index--)
				windows[index] = windows[index - 1U];
			windows[place] = surface;
		}
	}

	/* Succeeded: the windows. */
	return count;
}

/* Gives a window's body in a slot: under its title bar for a window whose title the compositor draws, the whole slot for one that draws its own. */
static void
arrange_body(
	const struct kwl_object *surface,
	const struct kwl_arrange_rect *slot,
	int32_t body[4])
{
	int decorated;

	/* The whole slot. */
	body[0] = slot->x;
	body[1] = slot->y;
	body[2] = slot->width;
	body[3] = slot->height;

	/* The compositor's title bar above the body. */
	decorated = kwl_decoration_server(surface);
	if (decorated) {
		body[1] += KWL_GLASS_TITLE + KWL_GLASS_GAP;
		body[3] -= KWL_GLASS_TITLE + KWL_GLASS_GAP;
	}
}

/* Glides a slot's window from where it is drawn now into the slot, and tells it the slot's size. */
static void
arrange_glide_to(
	struct kwl_server *server,
	struct arrange_slot *slot,
	unsigned index)
{
	int32_t body[4];

	UNUSED_PARAMETER(index);

	/* From where it is drawn now. */
	kwl_glass_body(server, slot->window, slot->from);

	/* To its slot, told its size. */
	arrange_body(slot->window, &slot->slot, body);
	kwl_glass_place_body(server, slot->window, body[0], body[1], body[2], body[3]);
	memcpy(slot->to, body, sizeof(slot->to));
	slot->glide_ms = kwl_milliseconds();
	server->dirty = 1;
}

/* Ends a desktop's arrangement mode: its windows are plain floating windows where they are. */
static void
arrange_end(
	struct kwl_server *server,
	unsigned desktop,
	const char *reason)
{
	/* A swap on it is over. */
	if (arrange_swap.window != NULL && arrange_swap.desktop == desktop)
		arrange_swap_end(server);

	/* Nothing of the arrangement is kept. */
	memset(&arrange_desktops[desktop], 0, sizeof(arrange_desktops[desktop]));
	server->dirty = 1;
	printf("KWL ARRANGE end desktop=%u reason=%s\n", desktop + 1U, reason);
}

/* Gives a window's slot in a desktop's arrangement, or -1. */
static int
arrange_slot_of(
	unsigned desktop,
	const struct kwl_object *surface)
{
	unsigned index;

	/* Each slot of the desktop. */
	for (index = 0U; index < arrange_desktops[desktop].count; index++) {
		if (arrange_desktops[desktop].slots[index].window == surface)
			return (int)index;
	}

	/* Succeeded: it is not arranged there. */
	return -1;
}

/* Draws a layout's small drawing: its slots for its usual number of windows, scaled down, outlined in the ink. */
static void
arrange_draw_icon(
	struct kwl_server *server,
	VkCommandBuffer command,
	unsigned layout,
	int32_t x,
	int32_t y,
	const float *ink,
	float alpha)
{
	struct kwl_arrange_rect area;
	struct kwl_arrange_rect slots[KWL_ARRANGE_MAX];
	float colour[4];
	unsigned count;
	unsigned made;
	unsigned index;

	/* Three windows, four in the grid, in an area five times the drawing's size. */
	count = 3U;
	if (layout == KWL_ARRANGE_GRID)
		count = 4U;
	area.x = -KWL_ARRANGE_MARGIN;
	area.y = -KWL_ARRANGE_MARGIN;
	area.width = ARRANGE_ICON_WIDTH * ARRANGE_ICON_SCALE + 2 * KWL_ARRANGE_MARGIN;
	area.height = ARRANGE_ICON_HEIGHT * ARRANGE_ICON_SCALE + 2 * KWL_ARRANGE_MARGIN;
	made = kwl_arrange_slots(layout, count, &area, slots);

	/* Each slot, scaled down, filled lightly with the ink. */
	memcpy(colour, ink, sizeof(colour));
	colour[3] = 0.55f * alpha;
	for (index = 0U; index < made; index++) {
		glass_draw_solid(server, command,
				 (float)x + (float)slots[index].x / (float)ARRANGE_ICON_SCALE,
				 (float)y + (float)slots[index].y / (float)ARRANGE_ICON_SCALE,
				 (float)slots[index].width / (float)ARRANGE_ICON_SCALE - 2.0f,
				 (float)slots[index].height / (float)ARRANGE_ICON_SCALE - 2.0f,
				 3.0f,
				 colour);
	}
}

/* Ends a swap being dragged without a release (the arrangement ended): the window stays where it is. */
static void
arrange_swap_end(
	struct kwl_server *server)
{
	/* No swap any more. */
	arrange_swap.window = NULL;
	server->dirty = 1;
}
