/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Display page (ws113-p006; the design is plan/ws113/phase006/phase.md):
 *
 *   Displays     Extend or Mirror (D-MODES: the two choices only), the
 *                arrangement of the displays in the desktop's plane, whose
 *                cards are dragged in the extended mode and snap next to
 *                each other on release (arrange.c), and a row for each
 *                display: its size, or that it is held back by the limit
 *                of the displays shown at once.  The choice is a draft
 *                until Apply; Revert takes the desktop's again.
 *   Brightness   the built-in panel's light, when the desktop can set it:
 *                a slider sent while dragged (every DISPLAY_LIGHT_MS) and
 *                at its release; the light keys move it too, which the
 *                page follows.
 *
 * Everything goes through the desktop's system (kl_system_displays_*).  A
 * choice made from displays that changed meanwhile is refused stale: the
 * page takes the new displays and says so.
 */

#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The page's controls (hit indices): the two modes, Apply, Revert, the light, the displays' cards from DISPLAY_CARD, and their on/off switches from DISPLAY_SHOWN (ws113-p014). */
#define DISPLAY_EXTEND		1
#define DISPLAY_MIRROR		2
#define DISPLAY_APPLY		3
#define DISPLAY_REVERT		4
#define DISPLAY_LIGHT		5
#define DISPLAY_CARD		10
#define DISPLAY_SHOWN		30

/* The cards' margin, the space between cards, the arrangement's height, a row's height and the text sizes. */
#define DISPLAY_PAD		18
#define DISPLAY_GAP		16
#define DISPLAY_BOX		200
#define DISPLAY_ROW		40
#define DISPLAY_BUTTONS		44
#define DISPLAY_TITLED		70
#define DISPLAY_TEXT_TITLE	15U
#define DISPLAY_TEXT_SMALL	13U

/* The room a row's switch takes at its right, and the switch's size (widgets.c's). */
#define DISPLAY_SWITCH		64
#define DISPLAY_TOGGLE		44
#define DISPLAY_TOGGLE_HEIGHT	24

/* How often a light being dragged is sent (ms). */
#define DISPLAY_LIGHT_MS	100U

static int display_plain(struct se_app *app, struct kl_canvas *canvas, int x, int top, int width);
static int display_arrangement(struct se_app *app, struct kl_canvas *canvas, int x, int y, int width);
static void display_card(struct se_app *app, struct kl_canvas *canvas, unsigned index, const struct se_arrange_rect *rect, unsigned arranged);
static int display_rows(struct se_app *app, struct kl_canvas *canvas, int x, int y, int width);
static int display_light(struct se_app *app, struct kl_canvas *canvas, int x, int top, int width);
static int display_light_index(const struct se_display *display);
static unsigned display_rects(const struct se_display *display, struct se_arrange_rect *rects, unsigned *slots);
static void display_spread(struct se_display *display);
static void display_take(struct se_app *app, int keep_draft);
static int display_same_set(const struct se_display *display, const struct kl_display *displays, size_t count);
static void display_apply(struct se_app *app);
static void display_send_light(struct se_app *app, int final);
static void display_send_shown(struct se_app *app, unsigned index);
static unsigned display_on_count(const struct se_display *display);
static void display_message(struct se_display *display, const char *text, int bad);
static uint64_t display_now_ms(void);

/*
 * Draws the Display page: the displays' card and, for a built-in panel,
 * the brightness card.  Returns the edge below them.
 */
int
se_display_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct se_display *display;
	const char *subtitle;
	kl_color ink;
	unsigned capable;
	int height;
	int y;
	int bx;

	/* Without the desktop's displays there is nothing to arrange: the screen's mode, read only. */
	display = &app->display;
	capable = 0U;
	if (app->system != NULL)
		capable = kl_system_capabilities(app->system) & KL_SYSTEM_HAS_DISPLAYS;
	if (capable == 0U || display->count == 0U)
		return display_plain(app, canvas, x, top, width);

	/* The displays' card: the modes, the arrangement, a row a display, Apply and Revert. */
	subtitle = kl_tr("Extend spreads the desktop over the displays. Mirror shows it on each.");
	height = DISPLAY_TITLED + DISPLAY_BUTTONS + 12 + DISPLAY_BOX + 12 + (int)display->count * DISPLAY_ROW + DISPLAY_BUTTONS + DISPLAY_PAD;
	y = se_card_begin(app, canvas, x, top, width, height, kl_tr("Displays"), subtitle);

	/* The two modes, the chosen one stressed. */
	bx = x + DISPLAY_PAD;
	bx += se_button_draw(app, canvas, bx, y + 6, kl_tr("Extend"), display->draft_mode == KL_DISPLAYS_EXTENDED, display->request == 0U, DISPLAY_EXTEND) + 8;
	bx += se_button_draw(app, canvas, bx, y + 6, kl_tr("Mirror"), display->draft_mode == KL_DISPLAYS_MIRROR, display->request == 0U, DISPLAY_MIRROR) + 16;

	/* With one display both modes show the same, which is said beside them. */
	if (display->count < 2U) {
		(void)kl_text_draw_fit(app->text, canvas, bx, y + 27, kl_tr("With one display both show the same."), DISPLAY_TEXT_SMALL, 0, x + width - DISPLAY_PAD - bx,
		    SE_COLOR_TEXT_SECONDARY);
	}

	/* Below the modes. */
	y += DISPLAY_BUTTONS + 12;

	/* The arrangement, then the rows. */
	y = display_arrangement(app, canvas, x + DISPLAY_PAD, y, width - 2 * DISPLAY_PAD);
	y = display_rows(app, canvas, x, y + 12, width);

	/* Apply and Revert, while the draft differs and nothing is awaited. */
	bx = x + width - DISPLAY_PAD - se_button_width(app, kl_tr("Apply"));
	(void)se_button_draw(app, canvas, bx, y + 6, kl_tr("Apply"), 1, display->edited && display->request == 0U, DISPLAY_APPLY);
	bx -= se_button_width(app, kl_tr("Revert")) + 8;
	(void)se_button_draw(app, canvas, bx, y + 6, kl_tr("Revert"), 0, display->edited && display->request == 0U, DISPLAY_REVERT);

	/* The last answer, under the card. */
	y = top + height;
	if (display->message[0] != '\0') {
		ink = SE_COLOR_TEXT_SECONDARY;
		if (display->message_bad)
			ink = SE_COLOR_BAD;
		(void)kl_text_draw_fit(app->text, canvas, x + 2, y + 18, display->message, DISPLAY_TEXT_SMALL, 0, width, ink);
		y += 30;
	}

	/* The light of a built-in panel, when the desktop can set it. */
	y = display_light(app, canvas, x, y + DISPLAY_GAP, width);

	/* The edge below the cards. */
	return y;
}

/*
 * Carries out a click on a control: a mode chosen, the draft applied or
 * reverted.  The displays' cards and the light are dragged instead.
 */
void
se_display_press(
	struct se_app *app,
	int index)
{
	struct se_display *display;

	/* Nothing while a request is awaited, or without the desktop's system. */
	display = &app->display;
	if (display->request != 0U || app->system == NULL)
		return;

	/* A mode: the draft's. */
	if (index == DISPLAY_EXTEND || index == DISPLAY_MIRROR) {
		display->draft_mode = KL_DISPLAYS_EXTENDED;
		if (index == DISPLAY_MIRROR)
			display->draft_mode = KL_DISPLAYS_MIRROR;
		if (display->draft_mode == KL_DISPLAYS_EXTENDED)
			display_spread(display);
		display->edited = 1U;
		if (display->draft_mode == KL_DISPLAYS_MIRROR && display->mode == KL_DISPLAYS_MIRROR)
			display->edited = 0U;
		if (display->draft_mode == KL_DISPLAYS_EXTENDED && display->mode == KL_DISPLAYS_EXTENDED)
			display->edited = (unsigned)(memcmp(display->draft, display->displays, display->count * sizeof(display->draft[0])) != 0);
		display->message[0] = '\0';
		app->dirty = 1;
		return;
	}

	/* The draft sent. */
	if (index == DISPLAY_APPLY && display->edited) {
		display_apply(app);
		return;
	}

	/* A display turned off or on, at once (ws113-p014). */
	if (index >= DISPLAY_SHOWN && (size_t)(index - DISPLAY_SHOWN) < display->count) {
		display_send_shown(app, (unsigned)(index - DISPLAY_SHOWN));
		return;
	}

	/* The desktop's choice again. */
	if (index == DISPLAY_REVERT) {
		display_take(app, 0);
		display->message[0] = '\0';
		app->dirty = 1;
	}
}

/*
 * Follows a drag on a display's card (in the extended mode: the card moves
 * with the pointer and snaps next to the others) or on the light's slider.
 */
void
se_display_drag(
	struct se_app *app,
	int index,
	int x,
	unsigned phase)
{
	struct se_display *display;
	struct se_arrange_rect rects[SE_ARRANGE_MAX];
	unsigned slots[SE_ARRANGE_MAX];
	unsigned arranged;
	unsigned moving;
	unsigned slot;
	int32_t plane_x;
	int32_t plane_y;
	int32_t snapped_x;
	int32_t snapped_y;
	float fraction;

	/* The light: the value under the pointer, sent now and then while held and at the release. */
	display = &app->display;
	if (index == DISPLAY_LIGHT) {
		fraction = se_slider_fraction(&display->slider, x);
		display->light = (unsigned)(fraction * 100.0f + 0.5f);
		display->light_dragging = 1;
		app->dirty = 1;
		if (phase == SE_DRAG_END) {
			display->light_dragging = 0;
			display_send_light(app, 1);
			return;
		}

		/* While held: now and then. */
		display_send_light(app, 0);
		return;
	}

	/* Only a display's card, in the extended mode, while nothing is awaited. */
	if (index < DISPLAY_CARD || (size_t)(index - DISPLAY_CARD) >= display->count)
		return;
	if (display->draft_mode != KL_DISPLAYS_EXTENDED || display->request != 0U)
		return;
	moving = (unsigned)(index - DISPLAY_CARD);

	/* The plane's point under the pointer, in the last frame's mapping. */
	se_arrange_from_box(&display->view, x, app->drag_y, &plane_x, &plane_y);

	/* The press: where on the card it was taken. */
	if (phase == SE_DRAG_START) {
		display->dragging = 1;
		display->drag_index = (int)moving;
		display->grab_x = plane_x - display->draft[moving].x;
		display->grab_y = plane_y - display->draft[moving].y;
		return;
	}

	/* Each move and the release: the card next to the others, nearest the pointer. */
	if (!display->dragging || display->drag_index != (int)moving)
		return;
	arranged = display_rects(display, rects, slots);
	for (slot = 0U; slot < arranged && slots[slot] != moving; slot++)
		continue;
	if (slot == arranged)
		return;
	se_arrange_snap(rects, arranged, slot, plane_x - display->grab_x, plane_y - display->grab_y, &snapped_x, &snapped_y);
	if (snapped_x != display->draft[moving].x || snapped_y != display->draft[moving].y) {
		display->draft[moving].x = snapped_x;
		display->draft[moving].y = snapped_y;
		display->edited = 1U;
		display->message[0] = '\0';
		app->dirty = 1;
	}

	/* The release ends the drag where it snapped. */
	if (phase == SE_DRAG_END) {
		display->dragging = 0;
		display->drag_index = -1;
		se_log("DISPLAY place %s x=%ld y=%ld", display->draft[moving].key, (long)display->draft[moving].x, (long)display->draft[moving].y);
	}
}

/*
 * Follows the displays: a new snapshot (a display plugged or unplugged, a
 * choice applied, the light changed) becomes the page's, keeping a draft
 * of the same displays.
 */
void
se_display_poll(
	struct se_app *app)
{
	struct se_display *display;

	/* Nothing without the desktop's system. */
	display = &app->display;
	if (app->system == NULL)
		return;

	/* The first snapshot, then each new one. */
	if (!display->taken || (app->system_changed & KL_SYSTEM_CHANGED_DISPLAYS) != 0U) {
		display->taken = 1;
		display_take(app, 1);
	}
}

/*
 * Takes the answer of a request the page asked.  Returns 1 when it was
 * one.
 */
int
se_display_result(
	struct se_app *app,
	uint32_t request,
	int error)
{
	struct se_display *display;

	/* A light's answer: only its failure is told. */
	display = &app->display;
	if (display->light_request != 0U && request == display->light_request) {
		display->light_request = 0U;
		se_log("DISPLAY light result errno=%d", error);
		if (error != 0)
			display_message(display, kl_tr("The brightness could not be changed."), 1);
		app->dirty = 1;
		return 1;
	}

	/* A display turned off or on (ws113-p014): only its failure is told. */
	if (display->shown_request != 0U && request == display->shown_request) {
		display->shown_request = 0U;
		se_log("DISPLAY shown result errno=%d", error);
		app->dirty = 1;
		switch (error) {
		case 0:
			display_message(display, "", 0);
			break;
		case EINVAL:
			display_message(display, kl_tr("At least one display stays on."), 1);
			break;
		case ENODEV:
			display_message(display, kl_tr("The display is not connected."), 1);
			break;
		case EPERM:
			display_message(display, kl_tr("The displays can be changed only in an unlocked session."), 1);
			break;
		default:
			display_message(display, kl_tr("The display could not be turned off or on."), 1);
			break;
		}

		/* The answer was the page's. */
		return 1;
	}

	/* Only the choice the page sent. */
	if (display->request == 0U || request != display->request)
		return 0;
	display->request = 0U;
	se_log("DISPLAY apply result errno=%d", error);
	app->dirty = 1;

	/* What it says; a choice applied (or refused) leaves the desktop's displays in the draft. */
	switch (error) {
	case 0:
		display_message(display, "", 0);
		display_take(app, 0);
		break;
	case ESTALE:
		display_take(app, 0);
		display_message(display, kl_tr("The displays changed. Arrange them again."), 1);
		break;
	case EINVAL:
		display_message(display, kl_tr("The displays must touch along an edge and not overlap."), 1);
		break;
	case EPERM:
		display_message(display, kl_tr("The displays can be changed only in an unlocked session."), 1);
		break;
	case ENOTSUP:
		display_message(display, kl_tr("This desktop cannot change the displays."), 1);
		break;
	default:
		display_message(display, kl_tr("The displays could not be changed."), 1);
		break;
	}

	/* Succeeded: the answer was the page's. */
	return 1;
}

/* Draws the screen's mode and the graphics device, read only, for a desktop that does not tell its displays; returns the edge below. */
static int
display_plain(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width)
{
	const char *mode;
	const char *graphics;
	int y;

	/* The values, or a dash for what is not known. */
	mode = app->about.display;
	if (mode[0] == '\0')
		mode = "-";
	graphics = app->about.graphics;
	if (graphics[0] == '\0')
		graphics = "-";

	/* The screen's card. */
	y = se_card_begin(app, canvas, x, top, width, se_card_height(2, 1), kl_tr("Screen"), NULL);
	y = se_row_value(app, canvas, x, y, width, kl_tr("Mode"), mode, 0);
	(void)se_row_value(app, canvas, x, y, width, kl_tr("Graphics"), graphics, 1);

	/* The edge below the card. */
	return top + se_card_height(2, 1);
}

/* Draws the arrangement's box: each display's card at its draft place, scaled; returns the edge below it. */
static int
display_arrangement(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int y,
	int width)
{
	struct se_display *display;
	struct se_arrange_rect rects[SE_ARRANGE_MAX];
	unsigned slots[SE_ARRANGE_MAX];
	unsigned arranged;
	unsigned slot;

	/* The box's ground. */
	display = &app->display;
	kl_canvas_round(canvas, (float)x, (float)y, (float)width, (float)DISPLAY_BOX, 10.0f, SE_COLOR_FIELD);

	/* The mapping: fitted again unless a card is being dragged (it would move under the pointer). */
	arranged = display_rects(display, rects, slots);
	if (!display->dragging)
		se_arrange_fit(rects, arranged, x, y, width, DISPLAY_BOX, &display->view);

	/* Each arranged display's card (one held back is told in its row only). */
	for (slot = 0U; slot < arranged; slot++)
		display_card(app, canvas, slots[slot], &rects[slot], arranged);

	/* The edge below the box. */
	return y + DISPLAY_BOX;
}

/*
 * Draws one display's card in the arrangement at its rectangle, a control
 * to drag in the extended mode when there is another arranged display.
 */
static void
display_card(
	struct se_app *app,
	struct kl_canvas *canvas,
	unsigned index,
	const struct se_arrange_rect *rect,
	unsigned arranged)
{
	struct se_display *display;
	const struct kl_display *shown;
	struct kl_rect hit;
	struct kl_text_line line;
	kl_color ground;
	kl_color ink;
	int x;
	int y;
	int width;
	int height;

	/* Its place in the box. */
	display = &app->display;
	shown = &display->draft[index];
	se_arrange_to_box(&display->view, rect, &x, &y, &width, &height);

	/* Its ground: the accent for the desktop's anchor. */
	ground = SE_COLOR_TILE;
	ink = SE_COLOR_TEXT;
	if ((shown->flags & KL_DISPLAY_ANCHOR) != 0U) {
		ground = SE_COLOR_ACCENT;
		ink = SE_COLOR_ACCENT_TEXT;
	}

	/* The card and its edge. */
	kl_canvas_round(canvas, (float)x, (float)y, (float)width, (float)height, 6.0f, ground);
	kl_canvas_round_border(canvas, (float)x, (float)y, (float)width, (float)height, 6.0f, 1.0f, SE_COLOR_CARD_EDGE);

	/* Its label, in the middle. */
	kl_text_metrics(app->text, DISPLAY_TEXT_SMALL, &line);
	(void)kl_text_draw_fit(app->text, canvas, x + 6, y + height / 2 + line.ascent / 2, shown->label, DISPLAY_TEXT_SMALL, 1, width - 12, ink);

	/* A card dragged in the extended mode, when there is another to arrange it with. */
	if (display->draft_mode != KL_DISPLAYS_EXTENDED || arranged < 2U)
		return;
	hit.x = x;
	hit.y = y;
	hit.width = width;
	hit.height = height;
	se_ui_hit(app, &hit, SE_HIT_CONTROL, DISPLAY_CARD + (int)index);
}

/* Draws a row for each display: its label, and its size or that it is held back; returns the edge below them. */
static int
display_rows(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int y,
	int width)
{
	struct se_display *display;
	const struct kl_display *shown;
	char value[96];
	char label[KL_DISPLAY_LABEL_MAX + 32];
	size_t index;
	unsigned on_count;
	int switches;
	int row_width;
	int on;
	int enabled;
	int top;

	/*
	 * Each display; in the extended mode each has a switch that turns it off
	 * or on at once (ws113-p014), the last one on kept on.
	 */
	display = &app->display;
	switches = display->mode == KL_DISPLAYS_EXTENDED && display->count > 1U;
	row_width = width;
	if (switches)
		row_width = width - DISPLAY_SWITCH;
	on_count = display_on_count(display);
	for (index = 0U; index < display->count; index++) {
		shown = &display->displays[index];

		/* Its label, marked when it is the machine's own. */
		(void)snprintf(label, sizeof(label), "%s", shown->label);
		if ((shown->flags & KL_DISPLAY_INTERNAL) != 0U)
			(void)snprintf(label, sizeof(label), "%s (%s)", shown->label, kl_tr("built in"));

		/* Its size and refresh, or why it is not shown. */
		(void)snprintf(value, sizeof(value), "%u x %u, %u Hz", shown->width, shown->height, (shown->refresh_mhz + 500U) / 1000U);
		if ((shown->flags & KL_DISPLAY_LIMITED) != 0U)
			(void)snprintf(value, sizeof(value), "%s", kl_tr("Not shown: the computer shows no more displays at once"));
		else if ((shown->flags & KL_DISPLAY_SHOWN) == 0U)
			(void)snprintf(value, sizeof(value), "%s", kl_tr("Off"));
		top = y;
		y = se_row_value(app, canvas, x, y, row_width, label, value, index + 1U == display->count);
		if (!switches)
			continue;

		/* Its switch: on unless turned off; the last one on, and any while a request is awaited, cannot be flipped. */
		on = (shown->flags & KL_DISPLAY_OFF) == 0U;
		enabled = display->shown_request == 0U && display->request == 0U;
		if (on && on_count <= 1U)
			enabled = 0;
		se_toggle_draw(app, canvas, x + width - DISPLAY_PAD - DISPLAY_TOGGLE, top + (DISPLAY_ROW - DISPLAY_TOGGLE_HEIGHT) / 2, on, enabled, DISPLAY_SHOWN + (int)index);
	}

	/* The edge below the rows. */
	return y;
}

/* Draws the brightness card when a display has a light the desktop can set; returns the edge below it (the top without one). */
static int
display_light(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct se_display *display;
	char percent[16];
	int index;
	int y;

	/* Only a display with a light. */
	display = &app->display;
	index = display_light_index(display);
	if (index < 0)
		return top;

	/* The card: the slider and the value. */
	y = se_card_begin(app, canvas, x, top, width, se_card_height(0, 1) + 60, kl_tr("Brightness"), display->displays[index].label);
	se_slider_draw(app, canvas, x + DISPLAY_PAD + 12, y + 18, width - 2 * DISPLAY_PAD - 90, (float)display->light / 100.0f, 1, DISPLAY_LIGHT, &display->slider);
	(void)snprintf(percent, sizeof(percent), "%u%%", display->light);
	(void)kl_text_draw_fit(app->text, canvas, x + width - DISPLAY_PAD - 50, y + 30, percent, DISPLAY_TEXT_TITLE, 0, 50, SE_COLOR_TEXT);

	/* The edge below the card. */
	return top + se_card_height(0, 1) + 60 + DISPLAY_GAP;
}

/* Finds the display whose light the desktop can set: its index, or -1. */
static int
display_light_index(
	const struct se_display *display)
{
	size_t index;

	/* The first with a light. */
	for (index = 0U; index < display->count; index++) {
		if ((display->displays[index].flags & KL_DISPLAY_BACKLIGHT) != 0U)
			return (int)index;
	}

	/* None. */
	return -1;
}

/*
 * Gives the rectangles of the displays arranged (each one not held back,
 * in the mirror mode all at the origin) and their indices in the draft;
 * returns how many.
 */
static unsigned
display_rects(
	const struct se_display *display,
	struct se_arrange_rect *rects,
	unsigned *slots)
{
	const struct kl_display *draft;
	unsigned count;
	size_t index;

	/* Each display not held back. */
	count = 0U;
	for (index = 0U; index < display->count && count < SE_ARRANGE_MAX; index++) {
		draft = &display->draft[index];
		if ((draft->flags & KL_DISPLAY_LIMITED) != 0U)
			continue;
		if ((draft->flags & KL_DISPLAY_OFF) != 0U && display->draft_mode == KL_DISPLAYS_EXTENDED)
			continue;

		/* Its draft place and size; the mirror's at the origin. */
		rects[count].x = draft->x;
		rects[count].y = draft->y;
		rects[count].width = draft->width;
		rects[count].height = draft->height;
		if (display->draft_mode == KL_DISPLAYS_MIRROR) {
			rects[count].x = 0;
			rects[count].y = 0;
		}

		/* Its index in the draft. */
		slots[count] = (unsigned)index;
		count++;
	}

	/* Succeeded: how many. */
	return count;
}

/*
 * Lays the arranged displays in a row, the anchor first, when any two of
 * them overlap (as the places of a mirror do): the extended mode chosen
 * from the mirror starts from places the compositor takes.
 */
static void
display_spread(
	struct se_display *display)
{
	struct se_arrange_rect rects[SE_ARRANGE_MAX];
	unsigned slots[SE_ARRANGE_MAX];
	unsigned arranged;
	unsigned first;
	unsigned second;
	unsigned slot;
	unsigned pass;
	int overlap;
	int64_t right;
	struct kl_display *draft;

	/* Whether any two overlap. */
	arranged = display_rects(display, rects, slots);
	overlap = 0;
	for (first = 0U; first < arranged; first++) {
		for (second = first + 1U; second < arranged; second++) {
			if ((int64_t)rects[first].x + rects[first].width <= rects[second].x ||
			    (int64_t)rects[second].x + rects[second].width <= rects[first].x)
				continue;
			if ((int64_t)rects[first].y + rects[first].height <= rects[second].y ||
			    (int64_t)rects[second].y + rects[second].height <= rects[first].y)
				continue;
			overlap = 1;
		}
	}

	/* Places that do not overlap are kept. */
	if (!overlap)
		return;

	/* A row from the origin: the anchor in the first pass, the others in the second. */
	right = 0;
	for (pass = 0U; pass < 2U; pass++) {
		for (slot = 0U; slot < arranged; slot++) {
			draft = &display->draft[slots[slot]];
			if (((draft->flags & KL_DISPLAY_ANCHOR) != 0U) != (pass == 0U))
				continue;
			draft->x = (int32_t)right;
			draft->y = 0;
			right += draft->width;
		}
	}
}

/*
 * Takes the desktop's displays.  keep_draft keeps an edited draft when
 * the displays are the same ones (only their light or a place someone
 * else applied changed); otherwise the draft is the desktop's.
 */
static void
display_take(
	struct se_app *app,
	int keep_draft)
{
	struct se_display *display;
	struct kl_display displays[KL_DISPLAYS_MAX];
	size_t count;
	size_t slot;
	int32_t x;
	int32_t y;
	int same;
	int index;

	/* The snapshot. */
	display = &app->display;
	count = kl_system_displays_get(app->system, displays, KL_DISPLAYS_MAX);
	same = display_same_set(display, displays, count);
	memcpy(display->displays, displays, count * sizeof(displays[0]));
	display->count = count;
	display->mode = kl_system_displays_mode(app->system);
	app->dirty = 1;

	/* The light shown, unless the user holds the slider. */
	index = display_light_index(display);
	if (index >= 0 && !display->light_dragging)
		display->light = display->displays[index].brightness;

	/* An edited draft of the same displays is kept (a card being dragged too), with what the desktop tells of each now but its place. */
	if (keep_draft && display->edited && same) {
		for (slot = 0U; slot < count; slot++) {
			x = display->draft[slot].x;
			y = display->draft[slot].y;
			display->draft[slot] = displays[slot];
			display->draft[slot].x = x;
			display->draft[slot].y = y;
		}

		/* Kept. */
		return;
	}

	/* The draft is the desktop's choice; a draft lost to other displays is said. */
	if (keep_draft && display->edited && !same)
		display_message(display, kl_tr("The displays changed. Arrange them again."), 1);
	memcpy(display->draft, displays, count * sizeof(displays[0]));
	display->draft_mode = display->mode;
	display->edited = 0U;
	display->dragging = 0;
	display->drag_index = -1;
}

/* Tells whether a snapshot names the same displays (by their keys, in the same order) as the page's. */
static int
display_same_set(
	const struct se_display *display,
	const struct kl_display *displays,
	size_t count)
{
	size_t index;
	int differs;

	/* The same number. */
	if (count != display->count)
		return 0;

	/* The same keys. */
	for (index = 0U; index < count; index++) {
		differs = strcmp(displays[index].key, display->displays[index].key);
		if (differs != 0)
			return 0;
	}

	/* The same displays. */
	return 1;
}

/* Sends the draft: the mode and, for the extended mode, every display's place. */
static void
display_apply(
	struct se_app *app)
{
	struct se_display *display;
	struct kl_display_place places[KL_DISPLAYS_MAX];
	size_t count;
	size_t index;
	int error;

	/* The places of the displays shown (a display held back keeps its own). */
	display = &app->display;
	count = 0U;
	for (index = 0U; index < display->count && display->draft_mode == KL_DISPLAYS_EXTENDED; index++) {
		if ((display->draft[index].flags & (KL_DISPLAY_LIMITED | KL_DISPLAY_OFF)) != 0U)
			continue;
		places[count].key = display->draft[index].key;
		places[count].x = display->draft[index].x;
		places[count].y = display->draft[index].y;
		count++;
	}

	/* Asked of the desktop. */
	error = kl_system_displays_apply(app->system, display->draft_mode, places, count, &display->request);
	se_log("DISPLAY apply mode=%u places=%u error=%d", display->draft_mode, (unsigned)count, error);
	if (error != 0) {
		display->request = 0U;
		display_message(display, kl_tr("This desktop cannot change the displays."), 1);
	}

	/* The page shows it. */
	app->dirty = 1;
}

/* Sends the light shown: at the release (final), or while dragged at most every DISPLAY_LIGHT_MS. */
static void
display_send_light(
	struct se_app *app,
	int final)
{
	struct se_display *display;
	uint64_t now;
	int index;
	int error;

	/* The display with the light, and not too often while dragged. */
	display = &app->display;
	index = display_light_index(display);
	if (index < 0 || app->system == NULL)
		return;
	now = display_now_ms();
	if (!final && now - display->light_sent_ms < DISPLAY_LIGHT_MS)
		return;
	display->light_sent_ms = now;

	/* Asked of the desktop; only the last request's answer is followed. */
	error = kl_system_displays_set_brightness(app->system, display->displays[index].key, display->light, &display->light_request);
	if (error != 0) {
		display->light_request = 0U;
		display_message(display, kl_tr("The brightness could not be changed."), 1);
	}

	/* The release is logged. */
	if (final)
		se_log("DISPLAY light percent=%u error=%d", display->light, error);
}

/* Keeps the page's last message. */
static void
display_message(
	struct se_display *display,
	const char *text,
	int bad)
{
	/* The text and whether it tells of a failure. */
	(void)snprintf(display->message, sizeof(display->message), "%s", text);
	display->message_bad = bad;
}

/* The monotonic time in milliseconds. */
static uint64_t
display_now_ms(void)
{
	struct timespec now;

	/* The clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Turns a display off when it is on, on when it is off (ws113-p014): asked of the desktop at once. */
static void
display_send_shown(
	struct se_app *app,
	unsigned index)
{
	struct se_display *display;
	unsigned shown;
	int error;

	/* Not while another request is awaited. */
	display = &app->display;
	if (display->shown_request != 0U || app->system == NULL)
		return;

	/* The other way from now. */
	display->message[0] = '\0';
	shown = (unsigned)((display->displays[index].flags & KL_DISPLAY_OFF) != 0U);
	error = kl_system_displays_set_shown(app->system, display->displays[index].key, shown, &display->shown_request);
	se_log("DISPLAY shown %s shown=%u error=%d", display->displays[index].key, shown, error);
	if (error != 0) {
		display->shown_request = 0U;
		display_message(display, kl_tr("This desktop cannot turn displays off."), 1);
	}

	/* The page shows it. */
	app->dirty = 1;
}

/* Counts the displays on: connected, not turned off, not held back (ws113-p014). */
static unsigned
display_on_count(
	const struct se_display *display)
{
	unsigned count;
	size_t index;

	/* Each display. */
	count = 0U;
	for (index = 0U; index < display->count; index++) {
		if ((display->displays[index].flags & (KL_DISPLAY_OFF | KL_DISPLAY_LIMITED)) == 0U)
			count++;
	}

	/* Succeeded: how many. */
	return count;
}
