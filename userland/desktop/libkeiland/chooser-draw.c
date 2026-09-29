/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser's looks (ws092-p003): where its parts go for the
 * window's size, and how they are drawn.
 *
 * The chooser looks like Files (userland/desktop/files): two cards on the
 * system's frosted glass, the sidebar of places on the left and the list
 * on the right, with Files' colours, row heights and text sizes.  Without
 * glass the cards stand on Files' pale ground.
 */

#include "chooser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The space around and between the cards, and their corners. */
#define DRAW_GAP		8
#define DRAW_RADIUS		16
#define DRAW_SIDEBAR_WIDTH	184

/* The space inside the content card, the location row, the list's header and the bottom bar. */
#define DRAW_PAD		12
#define DRAW_LOCATION		28
#define DRAW_HEADER		30
#define DRAW_BAR		52

/* The buttons' and fields' height, and the buttons' widths. */
#define DRAW_BUTTON		32
#define DRAW_ACCEPT_WIDTH	92
#define DRAW_CANCEL_WIDTH	84
#define DRAW_FILTER_WIDTH	136

/* The narrowest the name may be before the filter gives it its room (Save). */
#define DRAW_NAME_MIN		110

/* The question's card. */
#define DRAW_CARD_WIDTH		392
#define DRAW_CARD_HEIGHT	148

/* The widths of the list's Size and Modified columns, and the narrowest list that shows Modified. */
#define DRAW_SIZE_WIDTH		84
#define DRAW_MODIFIED_WIDTH	148
#define DRAW_MODIFIED_MIN	460

/* The text sizes (Files'). */
#define DRAW_TEXT_ROW		13U
#define DRAW_TEXT_HEADER	12U
#define DRAW_TEXT_SIDEBAR	14U
#define DRAW_TEXT_SECTION	11U
#define DRAW_TEXT_LOCATION	14U
#define DRAW_TEXT_BUTTON	13U
#define DRAW_TEXT_TITLE		15U

/* Files' colours. */
#define DRAW_GROUND_TOP		KL_RGB(0xeef2f7)
#define DRAW_GROUND_BOTTOM	KL_RGB(0xe6ebf3)
#define DRAW_PANEL		KL_RGB(0xffffff)
#define DRAW_PANEL_EDGE		KL_RGB(0xe2e7ef)
#define DRAW_SIDEBAR		KL_RGBA(0xffffff, 120)
#define DRAW_GLASS_SIDEBAR	KL_RGBA(0xffffff, 40)
#define DRAW_GLASS_CONTENT	KL_RGBA(0xffffff, 60)
#define DRAW_TEXT		KL_RGB(0x1e2632)
#define DRAW_TEXT_SECONDARY	KL_RGB(0x6b7585)
#define DRAW_TEXT_FAINT		KL_RGB(0xa3abb8)
#define DRAW_ICON		KL_RGB(0x46526a)
#define DRAW_ACCENT		KL_RGB(0x2f7cf6)
#define DRAW_SELECTION		KL_RGBA(0x2f7cf6, 40)
#define DRAW_SELECTION_INACTIVE	KL_RGBA(0x7a8699, 38)
#define DRAW_HOVER		KL_RGBA(0x5a6b85, 18)
#define DRAW_FOLDER		KL_RGB(0x5aa2f5)
#define DRAW_FOLDER_BACK	KL_RGB(0x3d86e0)
#define DRAW_SEPARATOR		KL_RGB(0xe8ecf2)
#define DRAW_FIELD_EDGE		KL_RGB(0xd5dbe4)
#define DRAW_DANGER		KL_RGB(0xe5484d)
#define DRAW_WARNING_TEXT	KL_RGB(0xc8313a)
#define DRAW_WARNING_GROUND	KL_RGB(0xfff1f1)
#define DRAW_WARNING_EDGE	KL_RGB(0xf6c9cb)

/* The separator between the parts of the location. */
#define DRAW_CRUMB		" \xe2\x80\xba "

/* The ellipsis put before a location cut at its start. */
#define DRAW_ELLIPSIS		"\xe2\x80\xa6"

static void draw_ground(struct kl_chooser *chooser, struct kl_canvas *canvas);
static void draw_sidebar(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
static void draw_place_icon(struct kl_canvas *canvas, enum kl_chooser_icon icon, int x, int y, uint32_t color);
static void draw_location(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
static void draw_location_text(struct kl_chooser *chooser, char *out, size_t size, struct kl_text *text, int width);
static void draw_list(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
static void draw_row(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas, int index, int y, int modified);
static void draw_item_icon(struct kl_canvas *canvas, int folder, int x, int y, int selected);
static void draw_size_text(int64_t size, char *out, size_t size_out);
static void draw_time_text(int64_t when, char *out, size_t size);
static void draw_bar(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
static void draw_field(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas, const struct kl_chooser_field *field, const struct kl_rect *rect, int focused);
static void draw_button(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas, const struct kl_rect *rect, const char *label, int primary, int enabled, enum kl_chooser_part part);
static void draw_message(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
static void draw_confirm(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
static void draw_text_centered(struct kl_text *text, struct kl_canvas *canvas, const struct kl_rect *rect, const char *string, unsigned pixels, int bold, uint32_t color);
static void draw_set(struct kl_rect *rect, int x, int y, int width, int height);

/*
 * Lays the window's parts out for its size.
 */
void
kl_chooser_layout(
	struct kl_chooser *chooser)
{
	struct kl_chooser_layout *layout;
	int middle;
	int right;
	int left;

	/* The two cards, side by side. */
	layout = &chooser->layout;
	draw_set(&layout->sidebar, DRAW_GAP, DRAW_GAP, DRAW_SIDEBAR_WIDTH, chooser->height - 2 * DRAW_GAP);
	draw_set(
		&layout->content,
		2 * DRAW_GAP + DRAW_SIDEBAR_WIDTH,
		DRAW_GAP,
		chooser->width - 3 * DRAW_GAP - DRAW_SIDEBAR_WIDTH,
		chooser->height - 2 * DRAW_GAP);

	/* The places, under the sidebar's title. */
	layout->places_top = layout->sidebar.y + 8 + 30;

	/* The row of the button to the folder above and the location. */
	draw_set(&layout->up, layout->content.x + DRAW_PAD, layout->content.y + 10, DRAW_LOCATION, DRAW_LOCATION);
	draw_set(
		&layout->location,
		layout->up.x + DRAW_LOCATION + 8,
		layout->up.y,
		layout->content.width - 2 * DRAW_PAD - DRAW_LOCATION - 8,
		DRAW_LOCATION);

	/* The list's header, the bottom bar, and the list between them. */
	draw_set(
		&layout->header,
		layout->content.x + DRAW_PAD,
		layout->up.y + DRAW_LOCATION + 8,
		layout->content.width - 2 * DRAW_PAD,
		DRAW_HEADER);
	draw_set(
		&layout->bar,
		layout->header.x,
		layout->content.y + layout->content.height - DRAW_BAR,
		layout->header.width,
		DRAW_BAR);
	draw_set(
		&layout->list,
		layout->header.x,
		layout->header.y + DRAW_HEADER,
		layout->header.width,
		layout->bar.y - layout->header.y - DRAW_HEADER - 4);

	/* The buttons from the right of the bar, centred in its height. */
	middle = layout->bar.y + (DRAW_BAR - DRAW_BUTTON) / 2 + 2;
	right = layout->bar.x + layout->bar.width;
	draw_set(&layout->accept, right - DRAW_ACCEPT_WIDTH, middle, DRAW_ACCEPT_WIDTH, DRAW_BUTTON);
	draw_set(&layout->cancel, layout->accept.x - 8 - DRAW_CANCEL_WIDTH, middle, DRAW_CANCEL_WIDTH, DRAW_BUTTON);

	/* Save: the filter left of the buttons and the name filling the rest; Open: the filter on the left. */
	left = layout->bar.x;
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE) {
		right = layout->cancel.x - 12;
		if (chooser->filter_count > 1U && right - left - 48 - DRAW_FILTER_WIDTH - 10 >= DRAW_NAME_MIN) {
			draw_set(&layout->filter, right - DRAW_FILTER_WIDTH, middle, DRAW_FILTER_WIDTH, DRAW_BUTTON);
			right = layout->filter.x - 10;
		} else {
			draw_set(&layout->filter, 0, 0, 0, 0);
		}

		/* The name takes the rest after its label. */
		draw_set(&layout->name, left + 48, middle, right - left - 48, DRAW_BUTTON);
	} else {
		draw_set(&layout->filter, left, middle, DRAW_FILTER_WIDTH, DRAW_BUTTON);
		if (chooser->filter_count < 2U)
			draw_set(&layout->filter, 0, 0, 0, 0);
		draw_set(&layout->name, 0, 0, 0, 0);
	}

	/* The question's card in the middle of the content, its buttons at its lower right. */
	draw_set(
		&layout->card,
		layout->content.x + (layout->content.width - DRAW_CARD_WIDTH) / 2,
		layout->content.y + (layout->content.height - DRAW_CARD_HEIGHT) / 2,
		DRAW_CARD_WIDTH,
		DRAW_CARD_HEIGHT);
	draw_set(
		&layout->replace,
		layout->card.x + layout->card.width - 18 - DRAW_ACCEPT_WIDTH,
		layout->card.y + layout->card.height - 16 - DRAW_BUTTON,
		DRAW_ACCEPT_WIDTH,
		DRAW_BUTTON);
	draw_set(&layout->keep, layout->replace.x - 8 - DRAW_CANCEL_WIDTH, layout->replace.y, DRAW_CANCEL_WIDTH, DRAW_BUTTON);
}

/*
 * Draws the whole window.
 */
void
kl_chooser_draw(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	/* The ground and the two cards. */
	kl_paint_unclip(canvas);
	draw_ground(chooser, canvas);

	/* The sidebar, the location, the list and the bar. */
	draw_sidebar(chooser, text, canvas);
	draw_location(chooser, text, canvas);
	draw_list(chooser, text, canvas);
	draw_bar(chooser, text, canvas);
	draw_message(chooser, text, canvas);

	/* The question over them all. */
	if (chooser->confirm)
		draw_confirm(chooser, text, canvas);
}

/*
 * Writes the glass panels under the two cards; returns how many (none
 * without glass).
 */
int
kl_chooser_glass_panels(
	const struct kl_chooser *chooser,
	struct keiland_glass_panel *panels,
	size_t capacity)
{
	/* Without glass, or room for both, no panels. */
	if (!chooser->glass || capacity < 2U)
		return 0;

	/* The sidebar's card. */
	panels[0].x = chooser->layout.sidebar.x;
	panels[0].y = chooser->layout.sidebar.y;
	panels[0].width = chooser->layout.sidebar.width;
	panels[0].height = chooser->layout.sidebar.height;
	panels[0].radius = DRAW_RADIUS;
	panels[0].kind = KEILAND_GLASS_CARD;

	/* The content's card. */
	panels[1].x = chooser->layout.content.x;
	panels[1].y = chooser->layout.content.y;
	panels[1].width = chooser->layout.content.width;
	panels[1].height = chooser->layout.content.height;
	panels[1].radius = DRAW_RADIUS;
	panels[1].kind = KEILAND_GLASS_CARD;

	/* Succeeded: two panels. */
	return 2;
}

/* Draws the ground: clear over glass (the cards' veils), Files' pale ground and white cards otherwise. */
static void
draw_ground(
	struct kl_chooser *chooser,
	struct kl_canvas *canvas)
{
	const struct kl_chooser_layout *layout;

	/* On glass: nothing between the cards, and a light veil on each. */
	layout = &chooser->layout;
	if (chooser->glass) {
		kl_paint_fill(canvas, 0, 0, canvas->width, canvas->height, 0U);
		kl_paint_round(canvas, layout->sidebar.x, layout->sidebar.y, layout->sidebar.width, layout->sidebar.height, DRAW_RADIUS, DRAW_GLASS_SIDEBAR);
		kl_paint_round(canvas, layout->content.x, layout->content.y, layout->content.width, layout->content.height, DRAW_RADIUS, DRAW_GLASS_CONTENT);
		return;
	}

	/* Without glass: the pale ground, the sidebar's veil and the white card. */
	kl_paint_gradient(canvas, 0, 0, canvas->width, canvas->height, DRAW_GROUND_TOP, DRAW_GROUND_BOTTOM);
	kl_paint_round(canvas, layout->sidebar.x, layout->sidebar.y, layout->sidebar.width, layout->sidebar.height, DRAW_RADIUS, DRAW_SIDEBAR);
	kl_paint_round_border(canvas, layout->sidebar.x, layout->sidebar.y, layout->sidebar.width, layout->sidebar.height, DRAW_RADIUS, KL_RGBA(0xffffff, 170));
	kl_paint_round(canvas, layout->content.x, layout->content.y + 2, layout->content.width, layout->content.height, DRAW_RADIUS, KL_RGBA(0x1f3a66, 20));
	kl_paint_round(canvas, layout->content.x, layout->content.y, layout->content.width, layout->content.height, DRAW_RADIUS, DRAW_PANEL);
	kl_paint_round_border(canvas, layout->content.x, layout->content.y, layout->content.width, layout->content.height, DRAW_RADIUS, DRAW_PANEL_EDGE);
}

/* Draws the sidebar: its title and each place, the place shown lit. */
static void
draw_sidebar(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	const struct kl_chooser_place *place;
	const struct kl_rect *panel;
	struct kl_rect row;
	uint32_t header;
	uint32_t ink;
	size_t index;
	int current;
	int same;

	/* The section's title: faint, a little darker on glass. */
	panel = &chooser->layout.sidebar;
	header = DRAW_TEXT_FAINT;
	if (chooser->glass)
		header = DRAW_TEXT_SECONDARY;
	(void)kl_text_draw(text, canvas, panel->x + 16, panel->y + 8 + 30 - 10, "Places", 6U, DRAW_TEXT_SECTION, 1, header);

	/* Each place, lit when it is shown or under the pointer. */
	kl_paint_clip(canvas, panel->x, panel->y, panel->width, panel->height);
	for (index = 0; index < chooser->place_count; index++) {
		place = &chooser->places[index];
		draw_set(&row, panel->x + 8, chooser->layout.places_top + (int)index * KL_CHOOSER_ROW_PLACE, panel->width - 16, KL_CHOOSER_ROW_PLACE);

		/* Whether it is the place shown. */
		current = 0;
		if (place->path[0] == '\0') {
			current = chooser->recent;
		} else if (!chooser->recent) {
			same = strcmp(place->path, chooser->folder);
			if (same == 0)
				current = 1;
		}

		/* Its ground and ink. */
		ink = DRAW_TEXT;
		if (current) {
			kl_paint_round(canvas, row.x, row.y, row.width, row.height, 9, DRAW_SELECTION);
			ink = DRAW_ACCENT;
		} else if (chooser->hover_part == KL_PART_PLACE && chooser->hover_index == (int)index) {
			kl_paint_round(canvas, row.x, row.y, row.width, row.height, 9, DRAW_HOVER);
		}

		/* Its icon and label. */
		draw_place_icon(canvas, place->icon, row.x + 8, row.y + 6, ink);
		(void)kl_text_draw_fit(text, canvas, row.x + 36, kl_text_center(DRAW_TEXT_SIDEBAR, row.y, row.height), place->label, DRAW_TEXT_SIDEBAR, current, row.width - 44, ink);
	}

	/* Drawing reaches the whole window again. */
	kl_paint_unclip(canvas);
}

/* Draws a place's icon in an 18-pixel square at a place, in lines of a colour. */
static void
draw_place_icon(
	struct kl_canvas *canvas,
	enum kl_chooser_icon icon,
	int x,
	int y,
	uint32_t color)
{
	double left;
	double top;

	/* The square's corner. */
	left = (double)x;
	top = (double)y;

	/* Each icon's few lines. */
	switch (icon) {
	case KL_ICON_RECENT:
		/* A clock. */
		kl_paint_ring(canvas, left + 9.0, top + 9.0, 7.0, 1.5, color);
		kl_paint_line(canvas, left + 9.0, top + 9.0, left + 9.0, top + 5.0, 1.5, color);
		kl_paint_line(canvas, left + 9.0, top + 9.0, left + 12.0, top + 11.0, 1.5, color);
		break;
	case KL_ICON_HOME:
		/* A house: its roof, walls and door. */
		kl_paint_line(canvas, left + 1.5, top + 9.0, left + 9.0, top + 2.0, 1.5, color);
		kl_paint_line(canvas, left + 9.0, top + 2.0, left + 16.5, top + 9.0, 1.5, color);
		kl_paint_line(canvas, left + 4.0, top + 7.5, left + 4.0, top + 16.0, 1.5, color);
		kl_paint_line(canvas, left + 14.0, top + 7.5, left + 14.0, top + 16.0, 1.5, color);
		kl_paint_line(canvas, left + 4.0, top + 16.0, left + 14.0, top + 16.0, 1.5, color);
		kl_paint_line(canvas, left + 9.0, top + 16.0, left + 9.0, top + 12.0, 1.5, color);
		break;
	case KL_ICON_DESKTOP:
		/* A screen on a stand. */
		kl_paint_round_border(canvas, x + 1, y + 2, 16, 11, 2, color);
		kl_paint_round_border(canvas, x + 2, y + 3, 14, 9, 1, color);
		kl_paint_line(canvas, left + 9.0, top + 13.0, left + 9.0, top + 16.0, 1.5, color);
		kl_paint_line(canvas, left + 5.0, top + 16.5, left + 13.0, top + 16.5, 1.5, color);
		break;
	case KL_ICON_DOCUMENTS:
		/* A page with lines. */
		kl_paint_round_border(canvas, x + 3, y + 1, 12, 16, 2, color);
		kl_paint_round_border(canvas, x + 4, y + 2, 10, 14, 1, color);
		kl_paint_line(canvas, left + 6.5, top + 6.0, left + 11.5, top + 6.0, 1.2, color);
		kl_paint_line(canvas, left + 6.5, top + 9.0, left + 11.5, top + 9.0, 1.2, color);
		kl_paint_line(canvas, left + 6.5, top + 12.0, left + 10.0, top + 12.0, 1.2, color);
		break;
	case KL_ICON_DOWNLOADS:
		/* An arrow into a tray. */
		kl_paint_line(canvas, left + 9.0, top + 2.0, left + 9.0, top + 11.0, 1.5, color);
		kl_paint_line(canvas, left + 5.0, top + 7.5, left + 9.0, top + 11.5, 1.5, color);
		kl_paint_line(canvas, left + 13.0, top + 7.5, left + 9.0, top + 11.5, 1.5, color);
		kl_paint_line(canvas, left + 2.0, top + 12.0, left + 2.0, top + 16.0, 1.5, color);
		kl_paint_line(canvas, left + 2.0, top + 16.0, left + 16.0, top + 16.0, 1.5, color);
		kl_paint_line(canvas, left + 16.0, top + 16.0, left + 16.0, top + 12.0, 1.5, color);
		break;
	case KL_ICON_COMPUTER:
		/* A disk with its light. */
		kl_paint_round_border(canvas, x + 1, y + 5, 16, 9, 2, color);
		kl_paint_round_border(canvas, x + 2, y + 6, 14, 7, 1, color);
		kl_paint_circle(canvas, left + 13.0, top + 9.5, 1.3, color);
		break;
	}
}

/* Draws the row of the button to the folder above and the location (or the path field). */
static void
draw_location(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	const struct kl_rect *up;
	const struct kl_rect *location;
	char shown[KL_CHOOSER_PATH_MAX];
	uint32_t ink;
	double middle_x;
	double middle_y;
	int top_folder;
	int enabled;

	/* The button: a round ground under the pointer, and an arrow, pale when there is nothing above. */
	up = &chooser->layout.up;
	enabled = 1;
	top_folder = strcmp(chooser->folder, "/");
	if (chooser->recent)
		enabled = 0;
	if (top_folder == 0)
		enabled = 0;
	ink = DRAW_ICON;
	if (!enabled)
		ink = DRAW_TEXT_FAINT;
	if (enabled && chooser->hover_part == KL_PART_UP)
		kl_paint_circle(canvas, (double)up->x + (double)up->width / 2.0, (double)up->y + (double)up->height / 2.0, (double)up->width / 2.0, DRAW_HOVER);
	middle_x = (double)up->x + (double)up->width / 2.0;
	middle_y = (double)up->y + (double)up->height / 2.0;
	kl_paint_line(canvas, middle_x, middle_y - 6.0, middle_x, middle_y + 6.0, 1.8, ink);
	kl_paint_line(canvas, middle_x - 5.0, middle_y - 1.0, middle_x, middle_y - 6.0, 1.8, ink);
	kl_paint_line(canvas, middle_x + 5.0, middle_y - 1.0, middle_x, middle_y - 6.0, 1.8, ink);

	/* The path field, while a path is typed. */
	location = &chooser->layout.location;
	if (chooser->focus == KL_FOCUS_PATH) {
		draw_field(chooser, text, canvas, &chooser->path, location, 1);
		return;
	}

	/* The location's parts, lit under the pointer (a click types a path). */
	if (chooser->hover_part == KL_PART_LOCATION)
		kl_paint_round(canvas, location->x, location->y, location->width, location->height, 8, DRAW_HOVER);
	draw_location_text(chooser, shown, sizeof(shown), text, location->width - 20);
	(void)kl_text_draw(text, canvas, location->x + 10, kl_text_center(DRAW_TEXT_LOCATION, location->y, location->height), shown, strlen(shown), DRAW_TEXT_LOCATION, 1, DRAW_TEXT);
}

/*
 * Writes the location as its parts from Home or Computer ("Home › Documents"),
 * dropping the first parts behind an ellipsis when it is wider than width.
 */
static void
draw_location_text(
	struct kl_chooser *chooser,
	char *out,
	size_t size,
	struct kl_text *text,
	int width)
{
	char parts[KL_CHOOSER_PATH_MAX];
	const char *home;
	const char *rest;
	const char *start;
	char *slash;
	size_t length;
	int inside;
	int same;
	int fits;

	/* Recent is its own name. */
	if (chooser->recent) {
		snprintf(out, size, "Recent");
		return;
	}

	/* The folder from Home when it is in the home folder, from Computer otherwise. */
	home = getenv("HOME");
	rest = chooser->folder;
	snprintf(parts, sizeof(parts), "Computer");
	inside = 0;
	length = 0;
	if (home != NULL && home[0] == '/' && home[1] != '\0') {
		length = strlen(home);
		same = strncmp(chooser->folder, home, length);
		if (same == 0) {
			if (chooser->folder[length] == '/' || chooser->folder[length] == '\0')
				inside = 1;
		}
	}

	/* A folder in the home folder starts from Home. */
	if (inside) {
		snprintf(parts, sizeof(parts), "Home");
		rest = chooser->folder + length;
	}

	/* Each part after it, joined by the separator. */
	while (*rest == '/')
		rest++;
	while (*rest != '\0') {
		length = strcspn(rest, "/");
		snprintf(parts + strlen(parts), sizeof(parts) - strlen(parts), "%s%.*s", DRAW_CRUMB, (int)length, rest);
		rest += length;
		while (*rest == '/')
			rest++;
	}

	/* The whole location when it fits. */
	start = parts;
	fits = kl_text_width(text, start, strlen(start), DRAW_TEXT_LOCATION, 1);
	if (fits <= width) {
		snprintf(out, size, "%s", parts);
		return;
	}

	/* Else the last parts that fit behind the ellipsis (at least the last one). */
	for (;;) {
		slash = strstr(start, DRAW_CRUMB);
		if (slash == NULL)
			break;
		start = slash + strlen(DRAW_CRUMB);
		snprintf(out, size, "%s%s%s", DRAW_ELLIPSIS, DRAW_CRUMB, start);
		fits = kl_text_width(text, out, strlen(out), DRAW_TEXT_LOCATION, 1);
		if (fits <= width)
			return;
	}

	/* Only the last part, cut to the width. */
	(void)kl_text_fit(text, start, DRAW_TEXT_LOCATION, 1, width, out, size);
}

/* Draws the list's header and its rows, or why it has none. */
static void
draw_list(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	const struct kl_rect *header;
	const struct kl_rect *list;
	char line[KL_CHOOSER_MESSAGE_MAX];
	struct kl_rect empty;
	double maximum;
	double total;
	double length;
	double top;
	int modified;
	int first;
	int index;
	int y;

	/* The columns' titles (Modified only when the list is wide enough). */
	header = &chooser->layout.header;
	list = &chooser->layout.list;
	modified = 0;
	if (list->width >= DRAW_MODIFIED_MIN)
		modified = 1;
	(void)kl_text_draw(text, canvas, header->x + 34, kl_text_center(DRAW_TEXT_HEADER, header->y, header->height), "Name", 4U, DRAW_TEXT_HEADER, 0, DRAW_TEXT_SECONDARY);
	if (modified) {
		(void)kl_text_draw(text, canvas, header->x + header->width - DRAW_MODIFIED_WIDTH + 4, kl_text_center(DRAW_TEXT_HEADER, header->y, header->height), "Modified", 8U, DRAW_TEXT_HEADER, 0, DRAW_TEXT_SECONDARY);
		(void)kl_text_draw(text, canvas, header->x + header->width - DRAW_MODIFIED_WIDTH - DRAW_SIZE_WIDTH + 4, kl_text_center(DRAW_TEXT_HEADER, header->y, header->height), "Size", 4U, DRAW_TEXT_HEADER, 0, DRAW_TEXT_SECONDARY);
	} else {
		(void)kl_text_draw(text, canvas, header->x + header->width - DRAW_SIZE_WIDTH + 4, kl_text_center(DRAW_TEXT_HEADER, header->y, header->height), "Size", 4U, DRAW_TEXT_HEADER, 0, DRAW_TEXT_SECONDARY);
	}

	/* The line under the titles. */
	kl_paint_fill(canvas, header->x, header->y + header->height - 1, header->width, 1, DRAW_SEPARATOR);

	/* An empty list says why. */
	if (chooser->count == 0U) {
		if (chooser->list_error != 0)
			snprintf(line, sizeof(line), "Can't open this folder: %s", strerror(chooser->list_error));
		else if (chooser->recent)
			snprintf(line, sizeof(line), "No recent files");
		else
			snprintf(line, sizeof(line), "This folder is empty");
		draw_set(&empty, list->x, list->y, list->width, list->height / 2);
		draw_text_centered(text, canvas, &empty, line, DRAW_TEXT_ROW, 0, DRAW_TEXT_FAINT);
		return;
	}

	/* The rows that show, within the list. */
	kl_paint_clip(canvas, list->x, list->y, list->width, list->height);
	first = (int)(chooser->scroll / (double)KL_CHOOSER_ROW);
	if (first < 0)
		first = 0;
	for (index = first; index < (int)chooser->count; index++) {
		y = list->y + index * KL_CHOOSER_ROW - (int)chooser->scroll;
		if (y >= list->y + list->height)
			break;
		draw_row(chooser, text, canvas, index, y, modified);
	}

	/* A thin scroll bar when the rows are taller than the list. */
	maximum = kl_chooser_scroll_max(chooser);
	if (maximum > 0.0) {
		total = (double)chooser->count * (double)KL_CHOOSER_ROW;
		length = (double)list->height * (double)list->height / total;
		if (length < 24.0)
			length = 24.0;
		top = (double)list->y + chooser->scroll / maximum * ((double)list->height - length);
		kl_paint_round(canvas, list->x + list->width - 5, (int)top, 4, (int)length, 2, KL_RGBA(0x46526a, 70));
	}

	/* Drawing reaches the whole window again. */
	kl_paint_unclip(canvas);
}

/* Draws one row of the list: its ground, icon, name, size and time. */
static void
draw_row(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas,
	int index,
	int y,
	int modified)
{
	const struct kl_chooser_entry *entry;
	const struct kl_rect *list;
	char cell[64];
	uint32_t ink;
	uint32_t faint;
	int baseline;
	int selected;
	int name_width;
	int width;

	/* The ground: the accent when selected (pale without the keyboard), faint under the pointer. */
	entry = &chooser->entries[index];
	list = &chooser->layout.list;
	ink = DRAW_TEXT;
	faint = DRAW_TEXT_SECONDARY;
	selected = 0;
	if (index == chooser->selected) {
		selected = 1;
		if (chooser->focused) {
			kl_paint_round(canvas, list->x, y + 1, list->width - 8, KL_CHOOSER_ROW - 2, 7, DRAW_ACCENT);
			ink = KL_RGB(0xffffff);
			faint = KL_RGBA(0xffffff, 210);
		} else {
			kl_paint_round(canvas, list->x, y + 1, list->width - 8, KL_CHOOSER_ROW - 2, 7, DRAW_SELECTION_INACTIVE);
		}
	} else if (chooser->hover_part == KL_PART_ROW && chooser->hover_index == index) {
		kl_paint_round(canvas, list->x, y + 1, list->width - 8, KL_CHOOSER_ROW - 2, 7, DRAW_HOVER);
	}

	/* The icon and the name. */
	baseline = kl_text_center(DRAW_TEXT_ROW, y, KL_CHOOSER_ROW);
	draw_item_icon(canvas, entry->folder, list->x + 6, y + 3, selected && chooser->focused);
	name_width = list->width - 34 - DRAW_SIZE_WIDTH - 8;
	if (modified)
		name_width -= DRAW_MODIFIED_WIDTH;
	(void)kl_text_draw_fit(text, canvas, list->x + 34, baseline, entry->name, DRAW_TEXT_ROW, 0, name_width - 8, ink);

	/* The size, to the right of its column ("--" for a folder). */
	if (entry->folder)
		snprintf(cell, sizeof(cell), "--");
	else
		draw_size_text(entry->size, cell, sizeof(cell));
	width = kl_text_width(text, cell, strlen(cell), DRAW_TEXT_ROW, 0);
	if (modified)
		(void)kl_text_draw(text, canvas, list->x + list->width - DRAW_MODIFIED_WIDTH - 16 - width, baseline, cell, strlen(cell), DRAW_TEXT_ROW, 0, faint);
	else
		(void)kl_text_draw(text, canvas, list->x + list->width - 16 - width, baseline, cell, strlen(cell), DRAW_TEXT_ROW, 0, faint);

	/* The time it changed. */
	if (modified) {
		draw_time_text(entry->modified, cell, sizeof(cell));
		(void)kl_text_draw_fit(text, canvas, list->x + list->width - DRAW_MODIFIED_WIDTH + 4, baseline, cell, DRAW_TEXT_ROW, 0, DRAW_MODIFIED_WIDTH - 16, faint);
	}
}

/* Draws an item's small icon in a 22-pixel square: a blue folder or a white page. */
static void
draw_item_icon(
	struct kl_canvas *canvas,
	int folder,
	int x,
	int y,
	int selected)
{
	uint32_t lines;

	/* A folder: its tab behind, its body in front. */
	if (folder) {
		kl_paint_round(canvas, x + 2, y + 3, 9, 6, 2, DRAW_FOLDER_BACK);
		kl_paint_round(canvas, x + 2, y + 5, 19, 14, 3, DRAW_FOLDER);
		kl_paint_fill(canvas, x + 3, y + 6, 17, 1, KL_RGBA(0xffffff, 90));
		return;
	}

	/* A page with a thin edge and three lines of text. */
	lines = KL_RGB(0xb8c1ce);
	if (selected)
		lines = DRAW_ACCENT;
	kl_paint_round(canvas, x + 4, y + 1, 15, 20, 3, KL_RGB(0xffffff));
	kl_paint_round_border(canvas, x + 4, y + 1, 15, 20, 3, KL_RGB(0xc5ccd6));
	kl_paint_fill(canvas, x + 7, y + 7, 9, 1, lines);
	kl_paint_fill(canvas, x + 7, y + 10, 9, 1, lines);
	kl_paint_fill(canvas, x + 7, y + 13, 6, 1, lines);
}

/* Writes a file's size as people read it (Files' units: bytes, KB, MB, GB of 1000). */
static void
draw_size_text(
	int64_t size,
	char *out,
	size_t size_out)
{
	/* Bytes, then the larger units with one decimal. */
	if (size < 1000) {
		snprintf(out, size_out, "%lld bytes", (long long)size);
	} else if (size < 1000000) {
		snprintf(out, size_out, "%.1f KB", (double)size / 1000.0);
	} else if (size < 1000000000) {
		snprintf(out, size_out, "%.1f MB", (double)size / 1000000.0);
	} else {
		snprintf(out, size_out, "%.1f GB", (double)size / 1000000000.0);
	}
}

/* Writes when an item changed: the date and the time. */
static void
draw_time_text(
	int64_t when,
	char *out,
	size_t size)
{
	struct tm *local;
	time_t seconds;

	/* The local time, or nothing when it cannot be told. */
	seconds = (time_t)when;
	local = localtime(&seconds);
	if (local == NULL) {
		snprintf(out, size, "--");
		return;
	}

	/* Written as a date and a time of day. */
	snprintf(out, size, "%04d-%02d-%02d %02d:%02d", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min);
}

/* Draws the bottom bar: the name (Save), the filter, and the two buttons. */
static void
draw_bar(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	const struct kl_chooser_layout *layout;
	const struct kl_rect *filter;
	const char *accept;
	uint32_t ink;
	double middle_x;
	double middle_y;
	int enabled;

	/* The line above the bar. */
	layout = &chooser->layout;
	kl_paint_fill(canvas, layout->bar.x, layout->bar.y, layout->bar.width, 1, DRAW_SEPARATOR);

	/* Save's name, with its label. */
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE) {
		(void)kl_text_draw(text, canvas, layout->bar.x + 2, kl_text_center(DRAW_TEXT_BUTTON, layout->name.y, layout->name.height), "Name", 4U, DRAW_TEXT_BUTTON, 0, DRAW_TEXT_SECONDARY);
		draw_field(chooser, text, canvas, &chooser->name, &layout->name, chooser->focus == KL_FOCUS_NAME);
	}

	/* The filter as a pill with its label and a small chevron, when there is a choice. */
	if (chooser->filter_count > 1U && layout->filter.width > 0) {
		filter = &layout->filter;
		ink = DRAW_TEXT;
		kl_paint_round(canvas, filter->x, filter->y, filter->width, filter->height, filter->height / 2, DRAW_HOVER);
		if (chooser->hover_part == KL_PART_FILTER)
			kl_paint_round(canvas, filter->x, filter->y, filter->width, filter->height, filter->height / 2, DRAW_HOVER);
		(void)kl_text_draw_fit(text, canvas, filter->x + 14, kl_text_center(DRAW_TEXT_BUTTON, filter->y, filter->height), chooser->filters[chooser->filter].label, DRAW_TEXT_BUTTON, 0, filter->width - 40, ink);
		middle_x = (double)(filter->x + filter->width - 18);
		middle_y = (double)filter->y + (double)filter->height / 2.0;
		kl_paint_line(canvas, middle_x - 4.0, middle_y - 2.0, middle_x, middle_y + 2.0, 1.5, DRAW_TEXT_SECONDARY);
		kl_paint_line(canvas, middle_x + 4.0, middle_y - 2.0, middle_x, middle_y + 2.0, 1.5, DRAW_TEXT_SECONDARY);
	}

	/* Cancel, and the main button (pale when it cannot be pressed). */
	accept = "Open";
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE)
		accept = "Save";
	enabled = kl_chooser_can_accept(chooser);
	draw_button(chooser, text, canvas, &layout->cancel, "Cancel", 0, 1, KL_PART_CANCEL);
	draw_button(chooser, text, canvas, &layout->accept, accept, 1, enabled, KL_PART_ACCEPT);
}

/* Draws a line of text being typed: its box, the selection, the text and the cursor. */
static void
draw_field(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas,
	const struct kl_chooser_field *field,
	const struct kl_rect *rect,
	int focused)
{
	size_t start;
	size_t end;
	int baseline;
	int shift;
	int inner;
	int left;
	int right;
	int cursor;

	/* The box, its edge the accent while it has the keyboard. */
	kl_paint_round(canvas, rect->x, rect->y, rect->width, rect->height, 8, DRAW_PANEL);
	if (focused) {
		kl_paint_round_border(canvas, rect->x, rect->y, rect->width, rect->height, 8, DRAW_ACCENT);
		kl_paint_round_border(canvas, rect->x + 1, rect->y + 1, rect->width - 2, rect->height - 2, 7, KL_RGBA(0x2f7cf6, 90));
	} else {
		kl_paint_round_border(canvas, rect->x, rect->y, rect->width, rect->height, 8, DRAW_FIELD_EDGE);
	}

	/* The text is moved left when the cursor would be past the box. */
	inner = rect->width - 20;
	cursor = kl_text_width(text, field->text, field->cursor, DRAW_TEXT_ROW, 0);
	shift = 0;
	if (cursor > inner)
		shift = cursor - inner;
	baseline = kl_text_center(DRAW_TEXT_ROW, rect->y, rect->height);
	kl_paint_clip(canvas, rect->x + 6, rect->y, rect->width - 12, rect->height);

	/* The selection, under the text. */
	start = field->anchor;
	end = field->cursor;
	if (start > end) {
		start = field->cursor;
		end = field->anchor;
	}

	/* A selection shows as a band under its characters (paler without the keyboard). */
	if (start != end) {
		left = kl_text_width(text, field->text, start, DRAW_TEXT_ROW, 0);
		right = kl_text_width(text, field->text, end, DRAW_TEXT_ROW, 0);
		if (focused)
			kl_paint_fill(canvas, rect->x + 10 - shift + left, rect->y + 7, right - left, rect->height - 14, KL_RGBA(0x2f7cf6, 70));
		else
			kl_paint_fill(canvas, rect->x + 10 - shift + left, rect->y + 7, right - left, rect->height - 14, KL_RGBA(0x7a8699, 50));
	}

	/* The text, and the cursor while the field has the keyboard. */
	(void)kl_text_draw(text, canvas, rect->x + 10 - shift, baseline, field->text, field->length, DRAW_TEXT_ROW, 0, DRAW_TEXT);
	if (focused && chooser->focused)
		kl_paint_fill(canvas, rect->x + 10 - shift + cursor, rect->y + 7, 2, rect->height - 14, DRAW_ACCENT);
	kl_paint_unclip(canvas);
}

/* Draws a button: the main one in the accent, the others white with an edge; lit under the pointer. */
static void
draw_button(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas,
	const struct kl_rect *rect,
	const char *label,
	int primary,
	int enabled,
	enum kl_chooser_part part)
{
	uint32_t ground;
	uint32_t ink;
	int hovered;

	/* Whether the pointer is over it. */
	hovered = 0;
	if (chooser->hover_part == part)
		hovered = 1;

	/* The main button: the accent, paler when it cannot be pressed, darker under the pointer. */
	if (primary) {
		ground = DRAW_ACCENT;
		if (part == KL_PART_REPLACE)
			ground = DRAW_DANGER;
		if (!enabled)
			ground = (ground & 0x00ffffffU) | 0x60000000U;
		kl_paint_round(canvas, rect->x, rect->y, rect->width, rect->height, 8, ground);
		if (hovered && enabled)
			kl_paint_round(canvas, rect->x, rect->y, rect->width, rect->height, 8, KL_RGBA(0x000000, 28));
		draw_text_centered(text, canvas, rect, label, DRAW_TEXT_BUTTON, 1, KL_RGB(0xffffff));
		return;
	}

	/* The others: white with an edge. */
	kl_paint_round(canvas, rect->x, rect->y, rect->width, rect->height, 8, DRAW_PANEL);
	if (hovered)
		kl_paint_round(canvas, rect->x, rect->y, rect->width, rect->height, 8, DRAW_HOVER);
	kl_paint_round_border(canvas, rect->x, rect->y, rect->width, rect->height, 8, DRAW_FIELD_EDGE);
	ink = DRAW_TEXT;
	draw_text_centered(text, canvas, rect, label, DRAW_TEXT_BUTTON, 0, ink);
}

/* Draws a refusal's message over the bottom of the list. */
static void
draw_message(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	const struct kl_rect *list;
	struct kl_rect chip;
	int width;

	/* No message, nothing. */
	if (chooser->message[0] == '\0')
		return;

	/* A pale red chip at the list's bottom, as wide as its words. */
	list = &chooser->layout.list;
	width = kl_text_width(text, chooser->message, strlen(chooser->message), DRAW_TEXT_ROW, 0) + 28;
	if (width > list->width)
		width = list->width;
	draw_set(&chip, list->x + (list->width - width) / 2, list->y + list->height - 38, width, 32);
	kl_paint_round(canvas, chip.x, chip.y, chip.width, chip.height, 10, DRAW_WARNING_GROUND);
	kl_paint_round_border(canvas, chip.x, chip.y, chip.width, chip.height, 10, DRAW_WARNING_EDGE);
	chip.x += 14;
	chip.width -= 28;
	(void)kl_text_draw_fit(text, canvas, chip.x, kl_text_center(DRAW_TEXT_ROW, chip.y, chip.height), chooser->message, DRAW_TEXT_ROW, 0, chip.width, DRAW_WARNING_TEXT);
}

/* Draws the question before a file is replaced: a veil over the content and a card. */
static void
draw_confirm(
	struct kl_chooser *chooser,
	struct kl_text *text,
	struct kl_canvas *canvas)
{
	const struct kl_chooser_layout *layout;
	const struct kl_rect *card;
	char line[KL_CHOOSER_PATH_MAX + 64];
	const char *name;
	const char *folder;
	char *slash;
	char where[KL_CHOOSER_PATH_MAX];

	/* The veil over the content. */
	layout = &chooser->layout;
	card = &layout->card;
	kl_paint_round(canvas, layout->content.x, layout->content.y, layout->content.width, layout->content.height, DRAW_RADIUS, KL_RGBA(0x0f172a, 46));

	/* The card and its soft shadow. */
	kl_paint_round(canvas, card->x - 2, card->y + 2, card->width + 4, card->height + 6, 16, KL_RGBA(0x1f3a66, 30));
	kl_paint_round(canvas, card->x, card->y, card->width, card->height, 14, DRAW_PANEL);
	kl_paint_round_border(canvas, card->x, card->y, card->width, card->height, 14, DRAW_PANEL_EDGE);

	/* The name and the folder of the file. */
	snprintf(where, sizeof(where), "%s", chooser->confirm_path);
	slash = strrchr(where, '/');
	name = where;
	folder = "/";
	if (slash != NULL) {
		*slash = '\0';
		name = slash + 1;
		if (where[0] != '\0') {
			folder = strrchr(where, '/');
			if (folder == NULL || folder[1] == '\0')
				folder = where;
			else
				folder++;
		}
	}

	/* The question and why it is asked. */
	snprintf(line, sizeof(line), "Replace \"%s\"?", name);
	(void)kl_text_draw_fit(text, canvas, card->x + 20, card->y + 36, line, DRAW_TEXT_TITLE, 1, card->width - 40, DRAW_TEXT);
	snprintf(line, sizeof(line), "A file with that name already exists in \"%s\".", folder);
	(void)kl_text_draw_fit(text, canvas, card->x + 20, card->y + 64, line, DRAW_TEXT_ROW, 0, card->width - 40, DRAW_TEXT_SECONDARY);
	(void)kl_text_draw_fit(text, canvas, card->x + 20, card->y + 84, "Replacing it overwrites its contents.", DRAW_TEXT_ROW, 0, card->width - 40, DRAW_TEXT_SECONDARY);

	/* Its two buttons. */
	draw_button(chooser, text, canvas, &layout->keep, "Cancel", 0, 1, KL_PART_KEEP);
	draw_button(chooser, text, canvas, &layout->replace, "Replace", 1, 1, KL_PART_REPLACE);
}

/* Draws a string in the middle of a rectangle. */
static void
draw_text_centered(
	struct kl_text *text,
	struct kl_canvas *canvas,
	const struct kl_rect *rect,
	const char *string,
	unsigned pixels,
	int bold,
	uint32_t color)
{
	int width;

	/* Its width, and the string set in the middle. */
	width = kl_text_width(text, string, strlen(string), pixels, bold);
	(void)kl_text_draw(text, canvas, rect->x + (rect->width - width) / 2, kl_text_center(pixels, rect->y, rect->height), string, strlen(string), pixels, bold, color);
}

/* Sets a rectangle. */
static void
draw_set(
	struct kl_rect *rect,
	int x,
	int y,
	int width,
	int height)
{
	/* Its corner and size. */
	rect->x = x;
	rect->y = y;
	rect->width = width;
	rect->height = height;
}
