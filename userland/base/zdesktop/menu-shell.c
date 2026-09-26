/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The System Menu as zdesktop draws and operates it (WS070,
 * plan/ws070/design.md section 6).
 *
 * A window's top-level items are drawn after its title: in its floating
 * title bar, or in the system bar while it is docked.  Pressing one opens
 * its popup; while a popup is open ("menu mode") zdesktop takes the pointer
 * and the keyboard: the pointer moves between the top-level items and the
 * rows, a submenu opens beside its row, a release or a click on a row
 * chooses it, and a press anywhere else closes the menu.  The keys move the
 * selection (up, down, left, right), choose (Enter, Space) and close (Esc,
 * F10).  F10 opens the focused window's first menu from the keyboard, and
 * the focused window's shortcuts choose their items directly.
 *
 * Hits are tested against the places the top-level items were last drawn
 * at, so that what the pointer is over is what the user sees.  A popup's
 * rows are made from the committed model each time they are drawn or
 * tested; the selection is kept as an item ID, so a commit that reorders
 * the rows keeps it, and one that removes the item drops it.
 */

#include "menu.h"

#include <stdio.h>
#include <string.h>

/* How many popups can be open (the model's tree is at most eight deep), and the rows of one. */
#define SHELL_DEPTH		8U
#define SHELL_ROWS		64U

/* The top-level items hit-tested in one frame, and the windows whose bar layout is logged. */
#define SHELL_HITS		128U
#define SHELL_LOGGED		32U

/* The "..." item that holds the top-level items the bar has no room for. */
#define SHELL_OVERFLOW		0xffffffffU

/* A top-level item: its padding on each side, the gap between two, and the gap after the title. */
#define ITEM_PADDING		10
#define ITEM_GAP		2
#define TITLE_GAP		18

/* A popup's rows, its padding, the gutter for check marks, its least width, corners and margin to the output's edge. */
#define ROW_HEIGHT		30
#define SEPARATOR_HEIGHT	11
#define POPUP_PADDING		6
#define POPUP_GUTTER		30
#define POPUP_MINIMUM		200
#define POPUP_RADIUS		10.0f
#define POPUP_MARGIN		8

/* The last evdev code the keysym tables cover (KEY_DELETE); the keys' codes are <uapi/input.h>'s. */
#define SHELL_KEY_LAST		111U

/* The keypad's Enter, which <uapi/input.h> does not name. */
#define SHELL_KEY_KPENTER	96U

/* The modifier bits of zdesktop's wl_keyboard.modifiers. */
#define SEAT_SHIFT		0x01U
#define SEAT_CTRL		0x04U
#define SEAT_ALT		0x08U
#define SEAT_META		0x40U

/*
 * One top-level item as it was last drawn: the window, the item (or
 * SHELL_OVERFLOW), whether in the system bar, the first top-level item the
 * overflow holds, the rectangle the pointer hits (the bar's whole height),
 * and where a popup under it starts.
 */
struct shell_hit {
	struct zwl_object *surface;
	uint32_t item;
	unsigned docked;
	unsigned first_hidden;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t popup_y;
};

/*
 * One open popup: the submenu whose children are its rows (SHELL_OVERFLOW
 * for the overflow's), the first hidden top-level item for the overflow,
 * where it was placed from (and, for a submenu, the parent popup's left
 * edge to flip to), where it is, and the selected row's item (0 for none).
 */
struct shell_popup {
	uint32_t parent;
	unsigned first_hidden;
	int32_t anchor_x;
	int32_t anchor_y;
	int32_t flip_x;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	uint32_t selected;
};

/* The last bar layout logged for a window (a checksum), so a layout is logged once. */
struct shell_logged {
	struct zwl_object *surface;
	uint32_t checksum;
};

/*
 * The menus' state.
 *
 * surface is the window whose menu is open (NULL when none is), docked
 * whether it opened from the system bar, and popups[0..depth-1] the open
 * popups from the top-level one down.  pressing says the press that opened
 * or entered the menu is still down, so that its release on a row chooses
 * it.  eaten_key is a key whose press zdesktop took, so that its release is
 * taken too.  The hits are rebuilt every frame (zwl_menu_frame).
 */
struct shell_menu {
	struct zwl_object *surface;
	unsigned docked;
	unsigned depth;
	struct shell_popup popups[SHELL_DEPTH];
	unsigned pressing;
	uint32_t eaten_key;
	unsigned hit_count;
	struct shell_hit hits[SHELL_HITS];
	struct shell_logged logged[SHELL_LOGGED];
};

/*
 * The one compositor's menus.  zdesktop runs one server per process, and
 * the menus live as long as it; the zero value is "no menu open, nothing
 * drawn yet".
 */
static struct shell_menu shell_menu;

/*
 * The XKB keysyms of the US layout's keys by evdev code, plain and with
 * Shift (zdesktop has no keymap; clients are sent evdev codes).  0 is a key
 * with no keysym here.
 */
static const uint32_t shell_plain_keysyms[SHELL_KEY_LAST + 1U] = {
	0, 0xff1b, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0xff08, 0xff09,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0xff0d, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' ', 0, 0xffbe, 0xffbf, 0xffc0, 0xffc1, 0xffc2,
	0xffc3, 0xffc4, 0xffc5, 0xffc6, 0xffc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0xffc8, 0xffc9, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0xff50, 0xff52, 0xff55, 0xff51, 0xff53, 0xff57, 0xff54, 0xff56, 0xff63, 0xffff
};

/* The same keys with Shift held: the symbols a US keyboard prints over the digits and punctuation. */
static const uint32_t shell_shifted_keysyms[SHELL_KEY_LAST + 1U] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '{', '}', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, ':', '"', '~', 0, '|', 0, 0, 0, 0,
	0, 0, 0, '<', '>', '?', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void shell_open(struct zwl_server *server, const struct shell_hit *hit, unsigned keyboard);
static void shell_close_from(struct zwl_server *server, unsigned level, unsigned notify);
static void shell_activate(struct zwl_server *server, const struct zwl_menu_item *item, const char *via);
static unsigned shell_rows(const struct zwl_menu_model *model, const struct shell_popup *popup, const struct zwl_menu_item **rows);
static void shell_layout(struct zwl_server *server, const struct zwl_menu_model *model, unsigned level);
static const struct zwl_menu_item *shell_row_at(struct zwl_server *server, const struct zwl_menu_model *model, unsigned level, int32_t x, int32_t y, int32_t *row_y);
static int shell_popup_at(int32_t x, int32_t y);
static void shell_select_step(struct zwl_server *server, const struct zwl_menu_model *model, int step);
static void shell_select_letter(struct zwl_server *server, const struct zwl_menu_model *model, uint32_t keysym);
static void shell_open_child(struct zwl_server *server, const struct zwl_menu_model *model, unsigned level, const struct zwl_menu_item *item, int32_t row_y, unsigned keyboard);
static void shell_choose_row(struct zwl_server *server, const struct zwl_menu_model *model, const struct zwl_menu_item *row, int32_t row_y, unsigned level, const char *via);
static void shell_step_top(struct zwl_server *server, int step);
static void shell_menu_key(struct zwl_server *server, const struct zwl_menu_model *model, uint32_t key);
static unsigned shell_selectable(const struct zwl_menu_item *item);
static unsigned shell_usable(const struct zwl_menu_model *model, const struct zwl_menu_item *item);
static const struct shell_hit *shell_hit_at(struct zwl_server *server, int32_t x, int32_t y);
static const struct shell_hit *shell_first_hit(struct zwl_object *surface);
static void shell_add_hit(struct zwl_object *surface, uint32_t item, unsigned docked, unsigned first_hidden, const struct zwl_menu_area *area, int32_t x, int32_t width);
static void shell_log_bar(struct zwl_object *surface, unsigned docked, const struct zwl_menu_area *area, uint32_t checksum);
static uint32_t shell_mix(uint32_t checksum, uint32_t value);
static void shell_log_popup(struct zwl_server *server, const struct zwl_menu_model *model, unsigned level);
static int shell_keysym(uint32_t key, uint32_t seat_modifiers, uint32_t *keysym, uint32_t *modifiers);
static const struct zwl_menu_item *shell_shortcut_item(const struct zwl_menu_model *model, uint32_t keysym, uint32_t modifiers);
static void shell_shortcut_text(const struct zwl_menu_item *item, char *text, size_t size);
static void shell_key_name(uint32_t keysym, char *text, size_t size);
static void shell_draw_popup(struct zwl_server *server, VkCommandBuffer command, const struct zwl_menu_model *model, unsigned level);
static void shell_draw_row(struct zwl_server *server, VkCommandBuffer command, const struct shell_popup *popup, const struct zwl_menu_item *row, int32_t row_y);

/*
 * Starts a frame: the top-level items are hit-tested where this frame draws them.
 */
void
zwl_menu_frame(
	struct zwl_server *server)
{
	/* The server is the one this file's state belongs to. */
	(void)server;

	/* The last frame's places are forgotten. */
	shell_menu.hit_count = 0;
}

/*
 * Tells how wide a window's title may be drawn when its menu shares the
 * room after it: two fifths of the room with a menu, all of it without.
 */
int32_t
zwl_menu_title_limit(
	struct zwl_server *server,
	struct zwl_object *surface,
	int32_t available)
{
	const struct zwl_menu_item *tops[1];
	const struct zwl_menu_model *model;
	struct zwl_object *place;
	unsigned count;

	/* The server is the one this file's state belongs to. */
	(void)server;

	/* A window without a menu keeps the whole room for its title. */
	model = zwl_menu_of_surface(surface, &place);
	if (model == NULL)
		return available;

	/* A menu with no visible top-level item takes no room either. */
	count = zwl_menu_children(model, ZWL_MENU_ROOT, tops, 1U);
	if (count == 0U)
		return available;

	/* Succeeded: the title's share. */
	return available * 2 / 5;
}

/*
 * Draws a window's top-level items in the area after its title (in its
 * title bar, or in the system bar when docked), with the pointer's hover
 * and the open item marked, and records where they are for the pointer.
 */
void
zwl_menu_draw_bar(
	struct zwl_server *server,
	VkCommandBuffer command,
	struct zwl_object *surface,
	unsigned docked,
	const struct zwl_menu_area *area,
	const float *ink,
	float fade)
{
	static const float hover[4] = { 1.0f, 1.0f, 1.0f, 0.62f };
	static const float opened[4] = { 0.25f, 0.52f, 0.98f, 0.20f };
	const struct zwl_menu_item *tops[SHELL_ROWS];
	const struct zwl_menu_model *model;
	struct zwl_object *place;
	float colour[4];
	float pill[4];
	unsigned count;
	unsigned shown;
	unsigned index;
	unsigned recording;
	unsigned open;
	uint32_t checksum;
	int32_t widths[SHELL_ROWS];
	int32_t more;
	int32_t x;
	int32_t pill_y;
	int32_t pill_height;
	int32_t baseline;

	/* The window's menu, and its top-level items that are not separators. */
	model = zwl_menu_of_surface(surface, &place);
	if (model == NULL)
		return;
	count = zwl_menu_children(model, ZWL_MENU_ROOT, tops, SHELL_ROWS);
	shown = 0;
	for (index = 0; index < count; index++) {
		/* A separator has no place among the top-level items. */
		if (tops[index]->type == ZWL_MENU_SEPARATOR)
			continue;
		tops[shown] = tops[index];
		widths[shown] = glass_text_width(server, SIZE_BAR, tops[index]->label) + 2 * ITEM_PADDING;
		shown++;
	}

	/* Only those are laid out. */
	count = shown;

	/* As many as fit; if not all do, the rest go under "..." at the end. */
	more = glass_text_width(server, SIZE_BAR, "...") + 2 * ITEM_PADDING;
	x = area->x;
	for (shown = 0; shown < count; shown++) {
		/* The last one needs no room for "..." after it. */
		if (shown + 1U == count && x + widths[shown] <= area->right)
			continue;
		if (x + widths[shown] + ITEM_GAP + more > area->right)
			break;
		x += widths[shown] + ITEM_GAP;
	}

	/*
	 * Places are recorded only where they are seen as they are: not while
	 * the desktop layer is moved (App Home, the desktops' slide) or the
	 * window docks or fades.
	 */
	recording = 0;
	if (!server->layer_on && server->anim == NULL && fade >= 1.0f)
		recording = 1;

	/* The pills sit in the middle of the bar. */
	pill_height = 28;
	if (docked)
		pill_height = 26;
	pill_y = area->top + (area->height - pill_height) / 2;
	baseline = area->top + area->height / 2 + 5;

	/* Each item that fits, then "..." when some do not. */
	checksum = shell_mix(2166136261U, docked);
	x = area->x;
	for (index = 0; index <= shown; index++) {
		/* The overflow is drawn only when something is under it. */
		if (index == shown && shown == count)
			break;

		/* The item's width. */
		if (index == shown)
			widths[index] = more;

		/* Whether its popup is open. */
		open = 0;
		if (shell_menu.surface == surface && shell_menu.docked == docked && shell_menu.depth > 0U) {
			if (index < shown && shell_menu.popups[0].parent == tops[index]->id)
				open = 1;
			if (index == shown && shell_menu.popups[0].parent == SHELL_OVERFLOW)
				open = 1;
		}

		/* The open item is tinted; the one under the pointer, with no menu open, is lit. */
		if (open) {
			memcpy(pill, opened, sizeof(pill));
			pill[3] *= fade;
			glass_draw_solid(server, command, (float)x, (float)pill_y, (float)widths[index], (float)pill_height, 7.0f, pill);
		} else if (shell_menu.surface == NULL &&
			   server->pointer_x >= x && server->pointer_x < x + widths[index] &&
			   server->pointer_y >= area->top && server->pointer_y < area->top + area->height) {
			memcpy(pill, hover, sizeof(pill));
			pill[3] *= fade;
			glass_draw_solid(server, command, (float)x, (float)pill_y, (float)widths[index], (float)pill_height, 7.0f, pill);
		}

		/* The label, paler when it cannot be chosen. */
		memcpy(colour, ink, sizeof(colour));
		if (index < shown && !tops[index]->enabled)
			colour[3] *= 0.4f;
		colour[3] *= fade;
		if (index < shown)
			glass_draw_text(server, command, SIZE_BAR, x + ITEM_PADDING, baseline, tops[index]->label, widths[index], colour);
		else
			glass_draw_text(server, command, SIZE_BAR, x + ITEM_PADDING, baseline, "...", widths[index], colour);

		/* Its place, for the pointer. */
		if (recording && index < shown)
			shell_add_hit(surface, tops[index]->id, docked, 0U, area, x, widths[index]);
		if (recording && index == shown)
			shell_add_hit(surface, SHELL_OVERFLOW, docked, shown, area, x, widths[index]);

		/* The layout's checksum, for the log. */
		if (index < shown)
			checksum = shell_mix(checksum, tops[index]->id);
		checksum = shell_mix(checksum, (uint32_t)(x - area->origin));
		checksum = shell_mix(checksum, (uint32_t)widths[index]);
		x += widths[index] + ITEM_GAP;
	}

	/* A new layout is logged for the tests that click the items. */
	if (recording)
		shell_log_bar(surface, docked, area, checksum);
}

/*
 * Draws the open popups over everything but the cursor, from the top-level
 * one down.
 */
void
zwl_menu_draw_popups(
	struct zwl_server *server,
	VkCommandBuffer command)
{
	const struct zwl_menu_model *model;
	struct zwl_object *place;
	unsigned level;

	/* Nothing is open. */
	if (shell_menu.surface == NULL)
		return;

	/* The window's model, which may have changed since the popups opened. */
	model = zwl_menu_of_surface(shell_menu.surface, &place);
	if (model == NULL)
		return;

	/* Each popup at its place for its current rows. */
	for (level = 0; level < shell_menu.depth; level++) {
		shell_layout(server, model, level);
		shell_draw_popup(server, command, model, level);
	}
}

/*
 * Handles a pointer button for the menus.  A press on a top-level item
 * opens its popup (or chooses an item with no children); in menu mode the
 * menus take every button.  Returns 1 when the button was the menus'.
 */
int
zwl_menu_button(
	struct zwl_server *server,
	uint32_t button,
	uint32_t state)
{
	const struct zwl_menu_model *model;
	const struct zwl_menu_item *row;
	const struct shell_hit *hit;
	struct zwl_object *place;
	int32_t row_y;
	int level;

	/* With no menu open only a left press on a top-level item is the menus'. */
	if (shell_menu.surface == NULL) {
		if (state == 0U || button != ZWL_BUTTON_LEFT)
			return 0;
		hit = shell_hit_at(server, server->pointer_x, server->pointer_y);
		if (hit == NULL)
			return 0;

		/* The window comes to the top (a floating one), and its menu opens. */
		if (!hit->docked)
			zwl_glass_raise(server, hit->surface);
		shell_open(server, hit, 0);
		shell_menu.pressing = 1;
		return 1;
	}

	/* The model of the open menu; a menu whose model went is closed. */
	model = zwl_menu_of_surface(shell_menu.surface, &place);
	if (model == NULL) {
		shell_close_from(server, 0U, 0U);
		return 1;
	}

	/* A release chooses the row under it, when the press was the menus'. */
	if (state == 0U) {
		if (!shell_menu.pressing)
			return 1;
		shell_menu.pressing = 0;
		level = shell_popup_at(server->pointer_x, server->pointer_y);
		if (level < 0)
			return 1;
		row = shell_row_at(server, model, (unsigned)level, server->pointer_x, server->pointer_y, &row_y);
		if (row != NULL)
			shell_choose_row(server, model, row, row_y, (unsigned)level, "pointer");
		return 1;
	}

	/* A press on a top-level item of the open menu closes it, or opens that item's popup. */
	hit = shell_hit_at(server, server->pointer_x, server->pointer_y);
	if (hit != NULL && hit->surface == shell_menu.surface && hit->docked == shell_menu.docked) {
		if (hit->item == shell_menu.popups[0].parent) {
			shell_close_from(server, 0U, 1U);
			return 1;
		}

		/* Another item's popup takes over. */
		shell_open(server, hit, 0);
		shell_menu.pressing = 1;
		return 1;
	}

	/* A press on a popup waits for its release to choose. */
	level = shell_popup_at(server->pointer_x, server->pointer_y);
	if (level >= 0) {
		shell_menu.pressing = 1;
		return 1;
	}

	/* A press anywhere else closes the menu and goes no further. */
	shell_close_from(server, 0U, 1U);

	/* Succeeded: the press was the menus'. */
	return 1;
}

/*
 * Follows the pointer in menu mode: another top-level item takes over, a
 * row is selected, and a submenu row opens its popup.  Returns 1 in menu
 * mode (the motion is the menus'), 0 otherwise.
 */
int
zwl_menu_motion(
	struct zwl_server *server)
{
	const struct zwl_menu_model *model;
	const struct zwl_menu_item *row;
	const struct shell_hit *hit;
	struct zwl_object *place;
	unsigned selectable;
	int32_t row_y;
	int level;

	/* Only menu mode takes the pointer. */
	if (shell_menu.surface == NULL)
		return 0;
	model = zwl_menu_of_surface(shell_menu.surface, &place);
	if (model == NULL)
		return 1;

	/* Another top-level item of the same bar opens instead. */
	hit = shell_hit_at(server, server->pointer_x, server->pointer_y);
	if (hit != NULL &&
	    hit->surface == shell_menu.surface &&
	    hit->docked == shell_menu.docked &&
	    hit->item != shell_menu.popups[0].parent) {
		shell_open(server, hit, 0);
		return 1;
	}

	/* Off every popup, the deepest one's selection goes (its submenus stay open). */
	level = shell_popup_at(server->pointer_x, server->pointer_y);
	if (level < 0) {
		shell_menu.popups[shell_menu.depth - 1U].selected = 0;
		return 1;
	}

	/* A row whose submenu is open already keeps it open; nothing changes. */
	row = shell_row_at(server, model, (unsigned)level, server->pointer_x, server->pointer_y, &row_y);
	if (row != NULL &&
	    shell_menu.depth > (unsigned)level + 1U &&
	    shell_menu.popups[level + 1].parent == row->id) {
		shell_menu.popups[level].selected = row->id;
		return 1;
	}

	/* The row under the pointer is selected when it can be chosen; deeper popups than its own close. */
	shell_close_from(server, (unsigned)level + 1U, 1U);
	shell_menu.popups[level].selected = 0;
	if (row == NULL)
		return 1;
	selectable = shell_selectable(row);
	if (!selectable)
		return 1;
	shell_menu.popups[level].selected = row->id;

	/* A submenu's popup opens beside its row. */
	if (row->type == ZWL_MENU_SUBMENU)
		shell_open_child(server, model, (unsigned)level, row, row_y, 0);

	/* Succeeded: the motion was the menus'. */
	return 1;
}

/*
 * Takes every key in menu mode and moves or chooses with it, and takes the
 * release of a key whose press the menus took.  Returns 1 when the key was
 * the menus'.
 */
int
zwl_menu_grab_key(
	struct zwl_server *server,
	uint32_t key,
	uint32_t state)
{
	const struct zwl_menu_model *model;
	struct zwl_object *place;

	/* The release of a key the menus took is theirs too. */
	if (state == 0U && shell_menu.eaten_key != 0U && key == shell_menu.eaten_key) {
		shell_menu.eaten_key = 0;
		return 1;
	}

	/* With no menu open, other keys go on. */
	if (shell_menu.surface == NULL)
		return 0;

	/* In menu mode every release is the menus', and a press is acted on. */
	if (state == 0U)
		return 1;
	shell_menu.eaten_key = key;

	/* The model of the open menu; a menu whose model went is closed. */
	model = zwl_menu_of_surface(shell_menu.surface, &place);
	if (model == NULL) {
		shell_close_from(server, 0U, 0U);
		return 1;
	}

	/* The key moves, chooses or closes. */
	shell_menu_key(server, model, key);
	server->dirty = 1;

	/* Succeeded: the key was the menus'. */
	return 1;
}

/*
 * Opens the focused window's first menu with F10, and chooses the item
 * whose shortcut a key press is.  Called after zdesktop's own shortcuts and
 * before the client.  Returns 1 when the key was the menus'.
 */
int
zwl_menu_key(
	struct zwl_server *server,
	uint32_t key,
	uint32_t state)
{
	const struct zwl_menu_model *model;
	const struct zwl_menu_item *item;
	const struct shell_hit *hit;
	struct zwl_object *place;
	uint32_t keysym;
	uint32_t modifiers;
	int known;

	/* Only a press, on a focused window with a menu. */
	if (state == 0U || server->focus == NULL)
		return 0;
	model = zwl_menu_of_surface(server->focus, &place);
	if (model == NULL)
		return 0;

	/* F10 alone opens the first menu from the keyboard, where it was last drawn. */
	if (key == KEY_F10 && (server->modifiers & (SEAT_SHIFT | SEAT_CTRL | SEAT_ALT | SEAT_META)) == 0U) {
		hit = shell_first_hit(server->focus);
		if (hit == NULL)
			return 0;
		shell_open(server, hit, 1);
		shell_menu.eaten_key = key;
		return 1;
	}

	/* A shortcut of a usable item chooses it. */
	known = shell_keysym(key, server->modifiers, &keysym, &modifiers);
	if (!known)
		return 0;
	item = shell_shortcut_item(model, keysym, modifiers);
	if (item == NULL)
		return 0;

	/* The item is chosen and the key's press and release go no further. */
	shell_menu.eaten_key = key;
	zwl_menu_send_activated(place, item, "shortcut");
	server->dirty = 1;

	/* Succeeded: the key was the menus'. */
	return 1;
}

/*
 * Closes the menu when what it belongs to has changed: its window is no
 * longer on top, docks, floats, goes fullscreen or away, App Home or
 * Wiseview opens, or an open submenu's item has gone; and drops a selection
 * that can no longer be chosen.
 */
void
zwl_menu_tick(
	struct zwl_server *server)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	const struct zwl_menu_model *model;
	const struct zwl_menu_item *item;
	struct zwl_object *surface;
	struct zwl_object *place;
	struct zwl_object *top;
	unsigned level;
	unsigned count;
	unsigned index;
	unsigned found;
	float home;

	/* Nothing is open. */
	surface = shell_menu.surface;
	if (surface == NULL)
		return;

	/* The window, its state and what covers it. */
	model = zwl_menu_of_surface(surface, &place);
	home = zwl_home_progress(server);
	top = zwl_top_window(server);
	if (model == NULL ||
	    surface != top ||
	    surface->maximized != shell_menu.docked ||
	    surface->fullscreen ||
	    surface->minimized ||
	    home > 0.0f ||
	    server->wiseview > 0.0f ||
	    server->wiseview_gesture ||
	    server->wiseview_moving) {
		shell_close_from(server, 0U, 1U);
		return;
	}

	/* Each popup's submenu must still be a row of the popup above it (the top-level one, a visible submenu). */
	for (level = 0; level < shell_menu.depth; level++) {
		/* The overflow's rows are the top level's. */
		if (shell_menu.popups[level].parent == SHELL_OVERFLOW)
			continue;
		item = zwl_menu_item(model, shell_menu.popups[level].parent);
		found = 0;
		if (item != NULL && item->visible && item->enabled && item->type == ZWL_MENU_SUBMENU)
			found = 1;

		/* A submenu popup's item is one of the rows above it. */
		if (found && level > 0U) {
			count = shell_rows(model, &shell_menu.popups[level - 1U], rows);
			found = 0;
			for (index = 0; index < count; index++) {
				/* The row is the submenu. */
				if (rows[index] == item)
					found = 1;
			}
		}

		/* This popup and those under it close. */
		if (!found) {
			shell_close_from(server, level, 1U);
			break;
		}
	}

	/* A selection that can no longer be chosen goes. */
	for (level = 0; level < shell_menu.depth; level++) {
		item = zwl_menu_item(model, shell_menu.popups[level].selected);
		found = 0;
		if (item != NULL)
			found = shell_usable(model, item);
		if (found)
			found = shell_selectable(item);

		/* The row is gone, hidden or disabled. */
		if (!found)
			shell_menu.popups[level].selected = 0;
	}
}

/*
 * Forgets an object that is going: its window's places in the bar, and the
 * open menu when it is the window, its place, its toplevel or its model.
 * No event is sent; the object may be the one that would receive it.
 */
void
zwl_menu_forget(
	struct zwl_server *server,
	struct zwl_object *object)
{
	struct zwl_object *surface;
	struct zwl_object *place;
	struct zwl_object *toplevel;
	unsigned index;
	unsigned kept;
	unsigned mine;

	/* The window's hits and its logged layout go. */
	kept = 0;
	for (index = 0; index < shell_menu.hit_count; index++) {
		if (shell_menu.hits[index].surface == object)
			continue;
		shell_menu.hits[kept] = shell_menu.hits[index];
		kept++;
	}

	/* The table keeps the others, and the window's logged layout is forgotten. */
	shell_menu.hit_count = kept;
	for (index = 0; index < SHELL_LOGGED; index++) {
		/* The window's entry is free again. */
		if (shell_menu.logged[index].surface == object)
			shell_menu.logged[index].surface = NULL;
	}

	/* Nothing is open. */
	surface = shell_menu.surface;
	if (surface == NULL)
		return;

	/* The open menu's window, toplevel, place and menu. */
	(void)zwl_menu_of_surface(surface, &place);
	toplevel = NULL;
	if (surface->role != NULL)
		toplevel = surface->role->top;
	mine = 0;
	if (object == surface || object == toplevel)
		mine = 1;
	if (place != NULL && (object == place || object == place->shown_menu))
		mine = 1;

	/* The menu closes without telling anyone. */
	if (mine)
		shell_close_from(server, 0U, 0U);
}

/*
 * Opens a top-level item's popup (closing any other), or chooses a
 * top-level item that has no children.  From the keyboard the first row
 * that can be chosen is selected.
 */
static void
shell_open(
	struct zwl_server *server,
	const struct shell_hit *hit,
	unsigned keyboard)
{
	const struct zwl_menu_model *model;
	const struct zwl_menu_item *item;
	struct shell_popup *popup;
	struct zwl_object *place;

	/* The window's model and the item. */
	model = zwl_menu_of_surface(hit->surface, &place);
	if (model == NULL)
		return;
	item = NULL;
	if (hit->item != SHELL_OVERFLOW) {
		item = zwl_menu_item(model, hit->item);
		if (item == NULL || !item->enabled)
			return;
	}

	/* Whatever was open closes. */
	shell_close_from(server, 0U, 1U);

	/* An item with no children is chosen at once. */
	if (item != NULL && item->type != ZWL_MENU_SUBMENU) {
		shell_activate(server, item, "pointer");
		return;
	}

	/* The top-level popup, under the item. */
	shell_menu.surface = hit->surface;
	shell_menu.docked = hit->docked;
	shell_menu.depth = 1;
	popup = &shell_menu.popups[0];
	memset(popup, 0, sizeof(*popup));
	popup->parent = hit->item;
	popup->first_hidden = hit->first_hidden;
	popup->anchor_x = hit->x - 4;
	popup->anchor_y = hit->popup_y;
	shell_layout(server, model, 0U);

	/* The client hears that the submenu opened; the log gives the rows. */
	if (item != NULL)
		zwl_menu_send_popup(place, item->id, 1U);
	shell_log_popup(server, model, 0U);

	/* From the keyboard the first row that can be chosen is selected. */
	if (keyboard)
		shell_select_step(server, model, 1);
	server->dirty = 1;
}

/*
 * Closes the popups from a level down (0 closes the menu), telling the
 * client of each submenu that closed when notify is set.
 */
static void
shell_close_from(
	struct zwl_server *server,
	unsigned level,
	unsigned notify)
{
	struct zwl_object *place;
	uint32_t parent;

	/* Nothing that deep is open. */
	if (level >= shell_menu.depth)
		return;

	/* The deepest first, as they were opened in reverse. */
	(void)zwl_menu_of_surface(shell_menu.surface, &place);
	while (shell_menu.depth > level) {
		shell_menu.depth--;
		parent = shell_menu.popups[shell_menu.depth].parent;
		if (notify && place != NULL && parent != SHELL_OVERFLOW)
			zwl_menu_send_popup(place, parent, 0U);
		printf("ZWL MENU close client=%llu surface=%u item=%u depth=%u\n", (unsigned long long)shell_menu.surface->client->number, shell_menu.surface->id, parent, shell_menu.depth);
	}

	/* The whole menu closed: menu mode ends. */
	if (shell_menu.depth == 0U) {
		shell_menu.surface = NULL;
		shell_menu.pressing = 0;
	}

	/* The output is drawn without them. */
	server->dirty = 1;
}

/* Chooses an item: the menu closes and its client hears the choice. */
static void
shell_activate(
	struct zwl_server *server,
	const struct zwl_menu_item *item,
	const char *via)
{
	struct zwl_object *surface;
	struct zwl_object *place;

	/* The place the choice goes to, found before the menu closes. */
	surface = shell_menu.surface;
	if (surface == NULL)
		surface = zwl_top_window(server);
	(void)zwl_menu_of_surface(surface, &place);

	/* The menu closes first (its submenus say so), then the choice is sent. */
	shell_close_from(server, 0U, 1U);
	if (place != NULL)
		zwl_menu_send_activated(place, item, via);
}

/* Lists a popup's rows: a submenu's visible children, or the overflow's top-level items. */
static unsigned
shell_rows(
	const struct zwl_menu_model *model,
	const struct shell_popup *popup,
	const struct zwl_menu_item **rows)
{
	const struct zwl_menu_item *tops[SHELL_ROWS];
	unsigned count;
	unsigned index;
	unsigned seen;
	unsigned kept;

	/* A submenu's rows are its children. */
	if (popup->parent != SHELL_OVERFLOW) {
		count = zwl_menu_children(model, popup->parent, rows, SHELL_ROWS);
		return count;
	}

	/* The overflow holds the top-level items from the first the bar had no room for. */
	count = zwl_menu_children(model, ZWL_MENU_ROOT, tops, SHELL_ROWS);
	seen = 0;
	kept = 0;
	for (index = 0; index < count; index++) {
		/* The bar skips separators, and so does the count of what it showed. */
		if (tops[index]->type == ZWL_MENU_SEPARATOR)
			continue;
		if (seen >= popup->first_hidden) {
			rows[kept] = tops[index];
			kept++;
		}

		/* One more of those the bar counted. */
		seen++;
	}

	/* Succeeded: the hidden top-level items. */
	return kept;
}

/*
 * Works out a popup's size from its rows and its place from where it was
 * opened: under its top-level item, or beside its row (on the other side
 * when it does not fit), kept inside the output.
 */
static void
shell_layout(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	unsigned level)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	struct shell_popup *popup;
	char shortcut[48];
	unsigned count;
	unsigned index;
	int32_t label;
	int32_t hint;
	int32_t width;
	int32_t height;
	int32_t right;
	int32_t bottom;

	/* The rows, and the widest of them. */
	popup = &shell_menu.popups[level];
	count = shell_rows(model, popup, rows);
	popup->width = POPUP_MINIMUM;
	height = 2 * POPUP_PADDING;
	for (index = 0; index < count; index++) {
		/* A separator is short and takes no width. */
		if (rows[index]->type == ZWL_MENU_SEPARATOR) {
			height += SEPARATOR_HEIGHT;
			continue;
		}

		/* The gutter, the label, a gap, the shortcut and the arrow. */
		height += ROW_HEIGHT;
		shell_shortcut_text(rows[index], shortcut, sizeof(shortcut));
		label = glass_text_width(server, SIZE_BAR, rows[index]->label);
		hint = glass_text_width(server, SIZE_BAR, shortcut);
		width = POPUP_GUTTER + label + 40 + hint + 14;
		if (rows[index]->type == ZWL_MENU_SUBMENU)
			width += 16;
		if (width > popup->width)
			popup->width = width;
	}

	/* Its height holds every row. */
	popup->height = height;

	/* Where it was opened from, pushed back from the right edge (a submenu flips to the left of its parent). */
	popup->x = popup->anchor_x;
	popup->y = popup->anchor_y;
	right = (int32_t)server->width - POPUP_MARGIN;
	if (popup->x + popup->width > right) {
		popup->x = right - popup->width;
		if (level > 0U)
			popup->x = popup->flip_x - popup->width + 4;
	}

	/* Never past the left edge. */
	if (popup->x < POPUP_MARGIN)
		popup->x = POPUP_MARGIN;

	/* And from the bottom, never over the system bar. */
	bottom = (int32_t)server->height - POPUP_MARGIN;
	if (popup->y + popup->height > bottom)
		popup->y = bottom - popup->height;
	if (popup->y < ZWL_GLASS_BAR + 4)
		popup->y = ZWL_GLASS_BAR + 4;
}

/* Finds the row of a popup at a point, and the row's top; NULL off its rows. */
static const struct zwl_menu_item *
shell_row_at(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	unsigned level,
	int32_t x,
	int32_t y,
	int32_t *row_y)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	struct shell_popup *popup;
	unsigned count;
	unsigned index;
	int32_t top;
	int32_t height;

	/* The popup where it is now. */
	shell_layout(server, model, level);
	popup = &shell_menu.popups[level];
	if (x < popup->x || x >= popup->x + popup->width)
		return NULL;

	/* The rows from the top. */
	count = shell_rows(model, popup, rows);
	top = popup->y + POPUP_PADDING;
	for (index = 0; index < count; index++) {
		height = ROW_HEIGHT;
		if (rows[index]->type == ZWL_MENU_SEPARATOR)
			height = SEPARATOR_HEIGHT;

		/* The row the point is in. */
		if (y >= top && y < top + height) {
			*row_y = top;
			return rows[index];
		}

		/* The next row is under it. */
		top += height;
	}

	/* The padding, or below the rows. */
	return NULL;
}

/* Finds the deepest open popup at a point; -1 when the point is on none. */
static int
shell_popup_at(
	int32_t x,
	int32_t y)
{
	const struct shell_popup *popup;
	int level;

	/* The deepest popup is drawn over the others. */
	for (level = (int)shell_menu.depth - 1; level >= 0; level--) {
		popup = &shell_menu.popups[level];
		if (x < popup->x || x >= popup->x + popup->width)
			continue;
		if (y < popup->y || y >= popup->y + popup->height)
			continue;
		return level;
	}

	/* None. */
	return -1;
}

/* Moves the deepest popup's selection to the next (step 1) or previous (-1) row that can be chosen, around the ends. */
static void
shell_select_step(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	int step)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	struct shell_popup *popup;
	unsigned count;
	unsigned tried;
	unsigned selectable;
	int at;
	int index;

	/* The deepest popup's rows, and where the selection is. */
	popup = &shell_menu.popups[shell_menu.depth - 1U];
	count = shell_rows(model, popup, rows);
	if (count == 0U)
		return;
	at = -1;
	for (index = 0; index < (int)count; index++) {
		/* The selected row. */
		if (rows[index]->id == popup->selected)
			at = index;
	}

	/* From before the first (or after the last) when nothing is selected. */
	if (at < 0 && step < 0)
		at = (int)count;

	/* The next row in the direction that can be chosen, trying each row once. */
	selectable = 0;
	for (tried = 0; tried < count; tried++) {
		at = (at + step + (int)count) % (int)count;
		selectable = shell_selectable(rows[at]);
		if (selectable)
			break;
	}

	/* The selection moves there, when there is such a row. */
	if (selectable)
		popup->selected = rows[at]->id;
	server->dirty = 1;
}

/* Moves the deepest popup's selection to the next row whose label starts with a letter or digit. */
static void
shell_select_letter(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	uint32_t keysym)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	struct shell_popup *popup;
	unsigned count;
	unsigned tried;
	unsigned selectable;
	int at;
	int index;
	int first;

	/* The deepest popup's rows, and where the selection is. */
	popup = &shell_menu.popups[shell_menu.depth - 1U];
	count = shell_rows(model, popup, rows);
	at = -1;
	for (index = 0; index < (int)count; index++) {
		/* The selected row. */
		if (rows[index]->id == popup->selected)
			at = index;
	}

	/* The next row after the selection whose first letter, in lower case, is the key's. */
	for (tried = 0; tried < count; tried++) {
		at = (at + 1) % (int)count;
		first = (unsigned char)rows[at]->label[0];
		if (first >= 'A' && first <= 'Z')
			first = first - 'A' + 'a';
		if ((uint32_t)first != keysym)
			continue;

		/* The first such row that can be chosen is selected. */
		selectable = shell_selectable(rows[at]);
		if (selectable) {
			popup->selected = rows[at]->id;
			break;
		}
	}

	/* The output shows the new selection. */
	server->dirty = 1;
}

/* Opens a submenu row's popup beside it, one level below the row's popup. */
static void
shell_open_child(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	unsigned level,
	const struct zwl_menu_item *item,
	int32_t row_y,
	unsigned keyboard)
{
	struct shell_popup *parent;
	struct shell_popup *popup;
	struct zwl_object *place;

	/* It is open already, or there is no deeper level. */
	if (shell_menu.depth > level + 1U && shell_menu.popups[level + 1U].parent == item->id)
		return;
	if (level + 1U >= SHELL_DEPTH)
		return;

	/* Deeper popups close; the new one goes beside the row. */
	shell_close_from(server, level + 1U, 1U);
	parent = &shell_menu.popups[level];
	popup = &shell_menu.popups[level + 1U];
	memset(popup, 0, sizeof(*popup));
	popup->parent = item->id;
	popup->anchor_x = parent->x + parent->width - 4;
	popup->anchor_y = row_y - POPUP_PADDING;
	popup->flip_x = parent->x;
	shell_menu.depth = level + 2U;
	shell_layout(server, model, level + 1U);

	/* The client hears it, the log gives the rows, and the keyboard selects the first. */
	(void)zwl_menu_of_surface(shell_menu.surface, &place);
	if (place != NULL)
		zwl_menu_send_popup(place, item->id, 1U);
	shell_log_popup(server, model, level + 1U);
	if (keyboard)
		shell_select_step(server, model, 1);
	server->dirty = 1;
}

/* Acts on a chosen row: a submenu opens, an item that can be chosen is activated. */
static void
shell_choose_row(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	const struct zwl_menu_item *row,
	int32_t row_y,
	unsigned level,
	const char *via)
{
	unsigned selectable;

	/* A separator or a disabled row does nothing, and the menu stays. */
	selectable = shell_selectable(row);
	if (!selectable)
		return;

	/* A submenu opens (from the keyboard, with its first row selected). */
	if (row->type == ZWL_MENU_SUBMENU) {
		shell_open_child(server, model, level, row, row_y, 1);
		return;
	}

	/* Anything else is chosen. */
	shell_activate(server, row, via);
}

/* Opens the next (step 1) or previous (-1) top-level item of the open bar, around the ends. */
static void
shell_step_top(
	struct zwl_server *server,
	int step)
{
	const struct shell_hit *tops[SHELL_HITS];
	unsigned count;
	unsigned index;
	int at;

	/* The open bar's top-level items, in order. */
	count = 0;
	at = 0;
	for (index = 0; index < shell_menu.hit_count; index++) {
		if (shell_menu.hits[index].surface != shell_menu.surface || shell_menu.hits[index].docked != shell_menu.docked)
			continue;
		if (shell_menu.hits[index].item == shell_menu.popups[0].parent)
			at = (int)count;
		tops[count] = &shell_menu.hits[index];
		count++;
	}

	/* The neighbour opens from the keyboard. */
	if (count == 0U)
		return;
	at = (at + step + (int)count) % (int)count;
	shell_open(server, tops[at], 1);
}

/* Acts on a key pressed in menu mode. */
static void
shell_menu_key(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	uint32_t key)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	const struct zwl_menu_item *row;
	struct shell_popup *popup;
	unsigned level;
	unsigned count;
	unsigned index;
	uint32_t keysym;
	uint32_t modifiers;
	int32_t row_y;
	int known;

	/* The deepest popup, its selected row and where the row is. */
	level = shell_menu.depth - 1U;
	popup = &shell_menu.popups[level];
	shell_layout(server, model, level);
	count = shell_rows(model, popup, rows);
	row = NULL;
	row_y = popup->y + POPUP_PADDING;
	for (index = 0; index < count; index++) {
		if (rows[index]->id == popup->selected) {
			row = rows[index];
			break;
		}

		/* The next row starts under this one. */
		row_y += ROW_HEIGHT;
		if (rows[index]->type == ZWL_MENU_SEPARATOR)
			row_y += SEPARATOR_HEIGHT - ROW_HEIGHT;
	}

	/* Each key's meaning. */
	switch (key) {
	case KEY_UP:
		shell_select_step(server, model, -1);
		break;
	case KEY_DOWN:
		shell_select_step(server, model, 1);
		break;
	case KEY_HOME:
		popup->selected = 0;
		shell_select_step(server, model, 1);
		break;
	case KEY_END:
		popup->selected = 0;
		shell_select_step(server, model, -1);
		break;
	case KEY_RIGHT:
		/* Into a selected submenu, otherwise to the next top-level menu. */
		if (row != NULL && row->type == ZWL_MENU_SUBMENU) {
			shell_open_child(server, model, level, row, row_y, 1);
			break;
		}

		/* The next top-level menu opens. */
		shell_step_top(server, 1);
		break;
	case KEY_LEFT:
		/* Out of a submenu, otherwise to the previous top-level menu. */
		if (level > 0U) {
			shell_close_from(server, level, 1U);
			break;
		}

		/* The previous top-level menu opens. */
		shell_step_top(server, -1);
		break;
	case KEY_ENTER:
	case SHELL_KEY_KPENTER:
	case KEY_SPACE:
		/* The selected row is chosen. */
		if (row != NULL)
			shell_choose_row(server, model, row, row_y, level, "key");
		break;
	case KEY_ESC:
		/* One popup closes. */
		shell_close_from(server, level, 1U);
		break;
	case KEY_F10:
		/* The whole menu closes. */
		shell_close_from(server, 0U, 1U);
		break;
	default:
		/* A letter or a digit selects the next row starting with it. */
		known = shell_keysym(key, 0U, &keysym, &modifiers);
		if (known && ((keysym >= 'a' && keysym <= 'z') || (keysym >= '0' && keysym <= '9')))
			shell_select_letter(server, model, keysym);
		break;
	}
}

/* Tells whether a row can be selected and chosen: not a separator, and enabled. */
static unsigned
shell_selectable(
	const struct zwl_menu_item *item)
{
	/* A separator is never chosen. */
	if (item->type == ZWL_MENU_SEPARATOR)
		return 0;

	/* A disabled item is shown but not chosen. */
	if (!item->enabled)
		return 0;

	/* Succeeded: the row can be chosen. */
	return 1;
}

/* Tells whether an item and all its submenus up to the top are visible and enabled. */
static unsigned
shell_usable(
	const struct zwl_menu_model *model,
	const struct zwl_menu_item *item)
{
	unsigned depth;

	/* Up the parents; the model is at most eight deep. */
	for (depth = 0; item != NULL && depth <= SHELL_DEPTH; depth++) {
		if (!item->visible || !item->enabled)
			return 0;
		if (item->parent == ZWL_MENU_ROOT)
			return 1;
		item = zwl_menu_item(model, item->parent);
	}

	/* A broken chain is not usable. */
	return 0;
}

/*
 * Finds the top-level item at a point, where the last frame drew it; NULL
 * when there is none, or when another window covers the title bar there
 * (the system bar is over every window).
 */
static const struct shell_hit *
shell_hit_at(
	struct zwl_server *server,
	int32_t x,
	int32_t y)
{
	const struct shell_hit *hit;
	struct zwl_object *top;
	unsigned index;

	/* Drawn later is drawn over, so the last one wins. */
	for (index = shell_menu.hit_count; index > 0U; index--) {
		hit = &shell_menu.hits[index - 1U];
		if (x < hit->x || x >= hit->x + hit->width)
			continue;
		if (y < hit->y || y >= hit->y + hit->height)
			continue;

		/* An item in a title bar counts only where its window is the one on top. */
		if (!hit->docked) {
			top = zwl_glass_window_at(server, x, y);
			if (top != hit->surface)
				return NULL;
		}

		/* Succeeded: the item under the point. */
		return hit;
	}

	/* None. */
	return NULL;
}

/* Finds a window's first top-level item as last drawn (in the system bar when it is docked). */
static const struct shell_hit *
shell_first_hit(
	struct zwl_object *surface)
{
	unsigned index;

	/* The first recorded for the window, in its present place. */
	for (index = 0; index < shell_menu.hit_count; index++) {
		/* The window's item, from the bar it is shown in. */
		if (shell_menu.hits[index].surface == surface && shell_menu.hits[index].docked == surface->maximized)
			return &shell_menu.hits[index];
	}

	/* The window's menu was not drawn. */
	return NULL;
}

/* Records where a top-level item was drawn: the bar's whole height over the item's width. */
static void
shell_add_hit(
	struct zwl_object *surface,
	uint32_t item,
	unsigned docked,
	unsigned first_hidden,
	const struct zwl_menu_area *area,
	int32_t x,
	int32_t width)
{
	struct shell_hit *hit;

	/* A full table drops the rest of the frame's items. */
	if (shell_menu.hit_count == SHELL_HITS)
		return;

	/* The item and its rectangle; a popup starts a little under the bar. */
	hit = &shell_menu.hits[shell_menu.hit_count];
	hit->surface = surface;
	hit->item = item;
	hit->docked = docked;
	hit->first_hidden = first_hidden;
	hit->x = x;
	hit->y = area->top;
	hit->width = width;
	hit->height = area->height;
	hit->popup_y = area->top + area->height + 6;
	shell_menu.hit_count++;
}

/* Logs a window's top-level items when their layout differs from the last one logged for it. */
static void
shell_log_bar(
	struct zwl_object *surface,
	unsigned docked,
	const struct zwl_menu_area *area,
	uint32_t checksum)
{
	const struct shell_hit *hit;
	struct shell_logged *entry;
	const char *where;
	unsigned index;
	uint32_t item;

	/* The window's entry, or a free one (the first when the table is full). */
	entry = &shell_menu.logged[0];
	for (index = 0; index < SHELL_LOGGED; index++) {
		if (shell_menu.logged[index].surface == surface) {
			entry = &shell_menu.logged[index];
			break;
		}

		/* A free entry serves until the window's own is found. */
		if (shell_menu.logged[index].surface == NULL)
			entry = &shell_menu.logged[index];
	}

	/* The same layout was logged already. */
	if (entry->surface == surface && entry->checksum == checksum)
		return;
	entry->surface = surface;
	entry->checksum = checksum;

	/* In the system bar, or in the window's own title bar. */
	where = "floating";
	if (docked)
		where = "docked";

	/* Each item of the window in this frame, from the bar's left edge (the overflow as item 0). */
	for (index = 0; index < shell_menu.hit_count; index++) {
		hit = &shell_menu.hits[index];
		if (hit->surface != surface || hit->docked != docked)
			continue;
		item = hit->item;
		if (item == SHELL_OVERFLOW)
			item = 0U;
		printf("ZWL MENU bar client=%llu surface=%u where=%s item=%u offset=%d top=%d width=%d height=%d\n", (unsigned long long)surface->client->number, surface->id,
		       where, item, hit->x - area->origin, hit->y, hit->width, hit->height);
	}
}

/* Mixes a value into a checksum (FNV-1a over its four bytes). */
static uint32_t
shell_mix(
	uint32_t checksum,
	uint32_t value)
{
	unsigned index;

	/* Each byte, lowest first. */
	for (index = 0; index < 4U; index++) {
		checksum ^= (value >> (8U * index)) & 0xffU;
		checksum *= 16777619U;
	}

	/* Succeeded: the new checksum. */
	return checksum;
}

/* Logs a popup that opened, and its rows, for the tests that click them. */
static void
shell_log_popup(
	struct zwl_server *server,
	const struct zwl_menu_model *model,
	unsigned level)
{
	const struct zwl_menu_item *rows[SHELL_ROWS];
	const struct shell_popup *popup;
	unsigned count;
	unsigned index;
	uint32_t parent;
	int32_t top;
	int32_t height;

	/* The popup (the overflow's as item 0). */
	shell_layout(server, model, level);
	popup = &shell_menu.popups[level];
	parent = popup->parent;
	if (parent == SHELL_OVERFLOW)
		parent = 0U;
	printf("ZWL MENU open client=%llu surface=%u item=%u depth=%u x=%d y=%d width=%d height=%d\n", (unsigned long long)shell_menu.surface->client->number, shell_menu.surface->id,
	       parent, level + 1U, popup->x, popup->y, popup->width, popup->height);

	/* Each row. */
	count = shell_rows(model, popup, rows);
	top = popup->y + POPUP_PADDING;
	for (index = 0; index < count; index++) {
		height = ROW_HEIGHT;
		if (rows[index]->type == ZWL_MENU_SEPARATOR)
			height = SEPARATOR_HEIGHT;
		printf("ZWL MENU row item=%u depth=%u y=%d height=%d\n", rows[index]->id, level + 1U, top, height);
		top += height;
	}
}

/*
 * Turns an evdev key and the seat's modifiers into a keysym and the
 * protocol's modifiers.  A letter keeps Shift as a modifier; a key whose
 * symbol Shift changes gives the changed symbol without Shift (Ctrl+Shift+=
 * is Ctrl++).  Returns 0 for a key the table does not know.
 */
static int
shell_keysym(
	uint32_t key,
	uint32_t seat_modifiers,
	uint32_t *keysym,
	uint32_t *modifiers)
{
	uint32_t plain;
	uint32_t shifted;

	/* The key's plain symbol. */
	if (key > SHELL_KEY_LAST)
		return 0;
	plain = shell_plain_keysyms[key];
	if (plain == 0U)
		return 0;

	/* The modifiers in the protocol's bits. */
	*modifiers = 0;
	if ((seat_modifiers & SEAT_SHIFT) != 0U)
		*modifiers |= ZWL_MENU_SHIFT;
	if ((seat_modifiers & SEAT_CTRL) != 0U)
		*modifiers |= ZWL_MENU_CTRL;
	if ((seat_modifiers & SEAT_ALT) != 0U)
		*modifiers |= ZWL_MENU_ALT;
	if ((seat_modifiers & SEAT_META) != 0U)
		*modifiers |= ZWL_MENU_SUPER;

	/* Shift changes the symbol of a digit or a punctuation key, and is then part of it. */
	*keysym = plain;
	shifted = shell_shifted_keysyms[key];
	if ((*modifiers & ZWL_MENU_SHIFT) != 0U && shifted != 0U) {
		*keysym = shifted;
		*modifiers &= ~ZWL_MENU_SHIFT;
	}

	/* Succeeded: the keysym and modifiers. */
	return 1;
}

/* Finds the usable item whose shortcut is a keysym with modifiers; NULL when none is. */
static const struct zwl_menu_item *
shell_shortcut_item(
	const struct zwl_menu_model *model,
	uint32_t keysym,
	uint32_t modifiers)
{
	const struct zwl_menu_item *item;
	uint32_t wanted;
	unsigned index;
	unsigned usable;

	/* Every item with a shortcut; a capital letter is the same key as its small one. */
	for (index = 0; index < model->count; index++) {
		item = &model->items[index];
		if (item->keysym == 0U || item->modifiers != modifiers)
			continue;
		wanted = item->keysym;
		if (wanted >= 'A' && wanted <= 'Z')
			wanted = wanted - 'A' + 'a';
		if (wanted != keysym)
			continue;

		/* A submenu or a separator is not chosen by a key, nor an item that cannot be chosen now. */
		if (item->type == ZWL_MENU_SUBMENU || item->type == ZWL_MENU_SEPARATOR)
			continue;
		usable = shell_usable(model, item);
		if (usable)
			return item;
	}

	/* No usable item has the shortcut. */
	return NULL;
}

/* Writes an item's shortcut as it is shown ("Ctrl+Shift+C"), or an empty string for none. */
static void
shell_shortcut_text(
	const struct zwl_menu_item *item,
	char *text,
	size_t size)
{
	char name[16];

	/* No shortcut. */
	text[0] = '\0';
	if (item->keysym == 0U)
		return;

	/* The modifiers in a fixed order: Ctrl, Alt, Shift, Super. */
	if ((item->modifiers & ZWL_MENU_CTRL) != 0U)
		(void)strncat(text, "Ctrl+", size - strlen(text) - 1U);
	if ((item->modifiers & ZWL_MENU_ALT) != 0U)
		(void)strncat(text, "Alt+", size - strlen(text) - 1U);
	if ((item->modifiers & ZWL_MENU_SHIFT) != 0U)
		(void)strncat(text, "Shift+", size - strlen(text) - 1U);
	if ((item->modifiers & ZWL_MENU_SUPER) != 0U)
		(void)strncat(text, "Super+", size - strlen(text) - 1U);

	/* Then the key's name. */
	shell_key_name(item->keysym, name, sizeof(name));
	(void)strncat(text, name, size - strlen(text) - 1U);
}

/* Writes the name a keysym is shown by: a letter in capitals, a symbol itself, F1..F12, or a key's name. */
static void
shell_key_name(
	uint32_t keysym,
	char *text,
	size_t size)
{
	/* The function keys. */
	if (keysym >= 0xffbeU && keysym <= 0xffc9U) {
		(void)snprintf(text, size, "F%u", keysym - 0xffbeU + 1U);
		return;
	}

	/* A letter in capitals. */
	if (keysym >= 'a' && keysym <= 'z') {
		(void)snprintf(text, size, "%c", (int)(keysym - 'a' + 'A'));
		return;
	}

	/* A space by its name. */
	if (keysym == ' ') {
		(void)snprintf(text, size, "Space");
		return;
	}

	/* Any other printable symbol as itself. */
	if (keysym > ' ' && keysym < 0x7fU) {
		(void)snprintf(text, size, "%c", (int)keysym);
		return;
	}

	/* The named keys. */
	switch (keysym) {
	case 0xff0dU:
		(void)snprintf(text, size, "Enter");
		break;
	case 0xff1bU:
		(void)snprintf(text, size, "Esc");
		break;
	case 0xff09U:
		(void)snprintf(text, size, "Tab");
		break;
	case 0xff08U:
		(void)snprintf(text, size, "Backspace");
		break;
	case 0xffffU:
		(void)snprintf(text, size, "Del");
		break;
	case 0xff63U:
		(void)snprintf(text, size, "Ins");
		break;
	case 0xff50U:
		(void)snprintf(text, size, "Home");
		break;
	case 0xff57U:
		(void)snprintf(text, size, "End");
		break;
	case 0xff55U:
		(void)snprintf(text, size, "PgUp");
		break;
	case 0xff56U:
		(void)snprintf(text, size, "PgDn");
		break;
	case 0xff51U:
		(void)snprintf(text, size, "Left");
		break;
	case 0xff52U:
		(void)snprintf(text, size, "Up");
		break;
	case 0xff53U:
		(void)snprintf(text, size, "Right");
		break;
	case 0xff54U:
		(void)snprintf(text, size, "Down");
		break;
	default:
		/* A key without a name here. */
		(void)snprintf(text, size, "?");
		break;
	}
}

/* Draws one popup: its shadow, its glass and its rows. */
static void
shell_draw_popup(
	struct zwl_server *server,
	VkCommandBuffer command,
	const struct zwl_menu_model *model,
	unsigned level)
{
	static const float line[4] = { 0.12f, 0.16f, 0.24f, 0.16f };
	const struct zwl_menu_item *rows[SHELL_ROWS];
	const struct shell_popup *popup;
	struct glass_shape shape;
	unsigned count;
	unsigned index;
	int32_t top;

	/* The shadow under it. */
	popup = &shell_menu.popups[level];
	glass_shape_init(&shape, (float)popup->x, (float)popup->y + 6.0f, (float)popup->width, (float)popup->height);
	shape.quad[0] -= 40.0f;
	shape.quad[1] -= 40.0f;
	shape.quad[2] += 80.0f;
	shape.quad[3] += 80.0f;
	shape.mode = MODE_SHADOW;
	shape.radius = POPUP_RADIUS;
	shape.soft = 18.0f;
	shape.color[0] = 0.10f;
	shape.color[1] = 0.18f;
	shape.color[2] = 0.35f;
	shape.color[3] = 0.24f;
	glass_shape_draw(server, command, &shape);

	/* The glass, whiter than a title bar so the rows read well over anything. */
	glass_shape_init(&shape, (float)popup->x, (float)popup->y, (float)popup->width, (float)popup->height);
	shape.mode = MODE_GLASS;
	shape.radius = POPUP_RADIUS;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = 0.86f;
	shape.edge = 0.85f;
	glass_shape_draw(server, command, &shape);

	/* The rows from the top: a separator is a thin line. */
	count = shell_rows(model, popup, rows);
	top = popup->y + POPUP_PADDING;
	for (index = 0; index < count; index++) {
		if (rows[index]->type == ZWL_MENU_SEPARATOR) {
			glass_draw_solid(server, command, (float)(popup->x + 12), (float)(top + SEPARATOR_HEIGHT / 2), (float)(popup->width - 24), 1.0f, 0.0f, line);
			top += SEPARATOR_HEIGHT;
			continue;
		}

		/* Any other row. */
		shell_draw_row(server, command, popup, rows[index], top);
		top += ROW_HEIGHT;
	}
}

/*
 * Draws one row: the selection's blue band, the check mark or radio dot in
 * the gutter, the label, the shortcut at the right and a submenu's arrow.
 */
static void
shell_draw_row(
	struct zwl_server *server,
	VkCommandBuffer command,
	const struct shell_popup *popup,
	const struct zwl_menu_item *row,
	int32_t row_y)
{
	static const float dark[4] = { 0.12f, 0.16f, 0.24f, 1.0f };
	static const float soft[4] = { 0.40f, 0.46f, 0.56f, 1.0f };
	static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	static const float blue[4] = { 0.25f, 0.52f, 0.98f, 1.0f };
	char shortcut[48];
	float ink[4];
	float hint[4];
	int32_t middle;
	int32_t baseline;
	int32_t right;
	int32_t width;
	int present;

	/* The row's middle and the text's baseline. */
	middle = row_y + ROW_HEIGHT / 2;
	baseline = middle + 5;
	right = popup->x + popup->width - 14;

	/* The selected row is a blue band with white text; a disabled row is pale. */
	memcpy(ink, dark, sizeof(ink));
	memcpy(hint, soft, sizeof(hint));
	if (row->id == popup->selected) {
		glass_draw_solid(server, command, (float)(popup->x + 5), (float)(row_y + 1), (float)(popup->width - 10), (float)(ROW_HEIGHT - 2), 6.0f, blue);
		memcpy(ink, white, sizeof(ink));
		memcpy(hint, white, sizeof(hint));
		hint[3] = 0.85f;
	} else if (!row->enabled) {
		ink[3] = 0.35f;
		hint[3] = 0.35f;
	}

	/* A checked checkbox has a check mark in the gutter (a small square without the glyph). */
	if (row->type == ZWL_MENU_CHECKBOX && row->checked) {
		present = glass_glyph_advance(server, SIZE_BAR, GLASS_CHECK_GLYPH);
		if (present > 0)
			glass_draw_glyph(server, command, SIZE_BAR, GLASS_CHECK_GLYPH, popup->x + 12, baseline, ink);
		else
			glass_draw_solid(server, command, (float)(popup->x + 13), (float)(middle - 4), 8.0f, 8.0f, 2.0f, ink);
	}

	/* A checked radio item has a dot. */
	if (row->type == ZWL_MENU_RADIO && row->checked)
		glass_draw_solid(server, command, (float)(popup->x + 13), (float)(middle - 4), 8.0f, 8.0f, 4.0f, ink);

	/* The label after the gutter. */
	glass_draw_text(server, command, SIZE_BAR, popup->x + POPUP_GUTTER, baseline, row->label, popup->width - POPUP_GUTTER - 14, ink);

	/* A submenu's arrow at the right edge ("›", or ">" without the glyph). */
	if (row->type == ZWL_MENU_SUBMENU) {
		present = glass_glyph_advance(server, SIZE_BAR, GLASS_ARROW_GLYPH);
		if (present > 0)
			glass_draw_glyph(server, command, SIZE_BAR, GLASS_ARROW_GLYPH, right - present, baseline, hint);
		else
			glass_draw_text(server, command, SIZE_BAR, right - 8, baseline, ">", 16, hint);
		right -= 16;
	}

	/* The shortcut, right-aligned before the arrow's place. */
	shell_shortcut_text(row, shortcut, sizeof(shortcut));
	if (shortcut[0] != '\0') {
		width = glass_text_width(server, SIZE_BAR, shortcut);
		glass_draw_text(server, command, SIZE_BAR, right - width, baseline, shortcut, width + 8, hint);
	}
}
