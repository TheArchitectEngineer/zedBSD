/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Ethernet page's IPv4 card of each wired interface (ws089-p022): how
 * it is configured (DHCP or a static address) and its router, the IPv6
 * that Kei does not have yet, and Edit; while edited, the choice of DHCP
 * or a static address, the address, subnet mask and router of a static
 * one, two DNS servers, and Apply and Cancel (wired.c checks and sends
 * them).  "Use DHCP" puts a static interface back on DHCP at once.
 *
 * Tab moves between the fields that count, Enter applies, Esc cancels.
 */

#include "settings.h"

#include <stdio.h>
#include <string.h>

/* The controls of the card (hit indices), past the network pages' (the networks around start at 100). */
#define WIRED_DHCP		300
#define WIRED_STATIC		301
#define WIRED_APPLY		302
#define WIRED_CANCEL		303
#define WIRED_FIELD_FIRST	310
#define WIRED_EDIT_FIRST	320
#define WIRED_USE_DHCP_FIRST	340

/* A field's row, where its box starts, the line of the message, the card's margin, and the text sizes. */
#define WIRED_ROW		52
#define WIRED_FIELD_X		210
#define WIRED_MESSAGE_LINE	30
#define WIRED_PAD		20
#define WIRED_TEXT_ROW		15U
#define WIRED_TEXT_SUB		13U

/* The fields' labels and what an empty one is for. */
static const char *const wired_labels[SE_WIRED_FIELDS] = { "IPv4 address", "Subnet mask", "Router", "DNS server 1", "DNS server 2" };
static const char *const wired_placeholders[SE_WIRED_FIELDS] = { "192.168.1.20", "255.255.255.0", "Optional", "From DHCP when empty", "Optional" };

static int wired_counts(const struct se_wired *wired, int index);
static void wired_field_draw(struct se_app *app, struct fm_canvas *canvas, int index, int x, int y, int width);
static const char *wired_mode_text(unsigned mode);

/*
 * Draws the IPv4 card of a wired interface, read-only or edited.  Returns
 * the edge below it.
 */
int
se_wired_card(
	struct se_app *app,
	struct fm_canvas *canvas,
	const struct kl_network_link *link,
	int x,
	int top,
	int width)
{
	struct se_wired *wired;
	char title[48];
	const char *router;
	fm_color ink;
	size_t which;
	int editing;
	int differs;
	int height;
	int right;
	int button;
	int index;
	int y;

	/* Edited or not. */
	wired = &app->wired;
	differs = strcmp(wired->interface, link->name);
	editing = wired->interface[0] != '\0' && differs == 0;
	(void)snprintf(title, sizeof(title), "IPv4 (%s)", link->name);
	which = (size_t)(link - app->network.links);

	/* Not edited: how it is configured, its router, the IPv6, and Edit (Use DHCP for a static one). */
	if (!editing) {
		height = se_card_height(3, 1) + 56;
		y = se_card_begin(app, canvas, x, top, width, height, title, NULL);
		y = se_row_value(app, canvas, x, y, width, "Configured by", wired_mode_text(link->wired_mode), 0);
		router = "-";
		if (link->router[0] != '\0')
			router = link->router;
		y = se_row_value(app, canvas, x, y, width, "Router", router, 0);
		y = se_row_value(app, canvas, x, y, width, "IPv6", "Not available on Kei yet", 1);
		right = x + width - WIRED_PAD;
		button = se_button_width(app, "Edit");
		(void)se_button_draw(app, canvas, right - button, y + 10, "Edit", 0, app->network.live, WIRED_EDIT_FIRST + (int)which);
		if (link->wired_mode == KL_WIRED_STATIC)
			(void)se_button_draw(app, canvas, right - button - 8 - se_button_width(app, "Use DHCP"), y + 10, "Use DHCP", 0, app->network.live, WIRED_USE_DHCP_FIRST + (int)which);
		return top + height;
	}

	/* Edited: the choice, the fields, the IPv6, the buttons and the message. */
	height = 64 + 56 + SE_WIRED_FIELDS * WIRED_ROW + 50 + 60 + WIRED_MESSAGE_LINE;
	y = se_card_begin(app, canvas, x, top, width, height, title, "DHCP gives the address by itself; a static one is typed here.");

	/* DHCP or a static address, the chosen one in the accent. */
	button = se_button_width(app, "DHCP");
	(void)se_button_draw(app, canvas, x + WIRED_FIELD_X, y + 10, "DHCP", wired->mode == KL_WIRED_DHCP, !wired->asked, WIRED_DHCP);
	(void)se_button_draw(app, canvas, x + WIRED_FIELD_X + button + 8, y + 10, "Static", wired->mode == KL_WIRED_STATIC, !wired->asked, WIRED_STATIC);
	(void)fm_text_draw_fit(app->text, canvas, x + WIRED_PAD, fm_text_center(WIRED_TEXT_ROW, y + 8, 36), "Configure", WIRED_TEXT_ROW, 0, WIRED_FIELD_X - 30, SE_COLOR_TEXT);
	y += 56;

	/* Each field (the address, mask and router faint with DHCP). */
	for (index = 0; index < SE_WIRED_FIELDS; index++) {
		wired_field_draw(app, canvas, index, x, y, width);
		y += WIRED_ROW;
	}

	/* The IPv6, which Kei does not have yet (ws089-p022: the user's decision), faint. */
	(void)fm_text_draw_fit(app->text, canvas, x + WIRED_PAD, fm_text_center(WIRED_TEXT_ROW, y + 8, 36), "IPv6", WIRED_TEXT_ROW, 0, WIRED_FIELD_X - 30, SE_COLOR_TEXT_FAINT);
	(void)fm_text_draw_fit(app->text, canvas, x + WIRED_FIELD_X, fm_text_center(WIRED_TEXT_ROW, y + 8, 36), "Not available on Kei yet", WIRED_TEXT_ROW, 0, width - WIRED_FIELD_X - WIRED_PAD, SE_COLOR_TEXT_FAINT);
	y += 50;

	/* Apply and Cancel at the right. */
	right = x + width - WIRED_PAD;
	button = se_button_width(app, "Cancel");
	(void)se_button_draw(app, canvas, right - button, y + 12, "Cancel", 0, 1, WIRED_CANCEL);
	(void)se_button_draw(app, canvas, right - button - 8 - se_button_width(app, "Apply"), y + 12, "Apply", 1, !wired->asked, WIRED_APPLY);
	y += 60;

	/* The message (red when it tells of a failure). */
	ink = SE_COLOR_TEXT_SECONDARY;
	if (wired->message_bad)
		ink = SE_COLOR_BAD;
	if (wired->message[0] != '\0')
		(void)fm_text_draw_fit(app->text, canvas, x + WIRED_PAD, y + 18, wired->message, WIRED_TEXT_SUB, 0, width - 2 * WIRED_PAD, ink);

	/* The edge below the card. */
	return top + height;
}

/*
 * Carries out a click on a control of the Ethernet page: the IPv4 cards'
 * own, or the network pages' (se_network_press).
 */
void
se_wired_press(
	struct se_app *app,
	int index)
{
	struct se_wired *wired;
	struct se_network *network;
	size_t which;
	int counts;

	/* Edit opens the editor on its interface. */
	wired = &app->wired;
	network = &app->network;
	if (index >= WIRED_EDIT_FIRST && index < WIRED_EDIT_FIRST + (int)SE_NETWORK_LINKS) {
		which = (size_t)(index - WIRED_EDIT_FIRST);
		if (which < network->link_count && !wired->asked)
			se_wired_edit(app, &network->links[which]);
		return;
	}

	/* Use DHCP puts a static interface back on DHCP at once (its DNS servers kept). */
	if (index >= WIRED_USE_DHCP_FIRST && index < WIRED_USE_DHCP_FIRST + (int)SE_NETWORK_LINKS) {
		which = (size_t)(index - WIRED_USE_DHCP_FIRST);
		if (which < network->link_count && !wired->asked) {
			se_wired_edit(app, &network->links[which]);
			wired->mode = KL_WIRED_DHCP;
			se_wired_apply(app);
		}

		/* Taken. */
		return;
	}

	/* A field takes the keyboard (one that does not count with DHCP does not). */
	if (index >= WIRED_FIELD_FIRST && index < WIRED_FIELD_FIRST + SE_WIRED_FIELDS) {
		counts = wired_counts(wired, index - WIRED_FIELD_FIRST);
		if (counts)
			wired->focus = index - WIRED_FIELD_FIRST;
		app->dirty = 1;
		return;
	}

	/* The editor's buttons. */
	switch (index) {
	case WIRED_DHCP:
		wired->mode = KL_WIRED_DHCP;
		counts = wired_counts(wired, wired->focus);
		if (!counts)
			wired->focus = SE_WIRED_DNS1;
		app->dirty = 1;
		return;
	case WIRED_STATIC:
		wired->mode = KL_WIRED_STATIC;
		app->dirty = 1;
		return;
	case WIRED_APPLY:
		se_wired_apply(app);
		return;
	case WIRED_CANCEL:
		se_wired_cancel(app);
		return;
	default:
		break;
	}

	/* Every other control is the network pages'. */
	se_network_press(app, index);
}

/*
 * Takes a key on the Ethernet page while an interface is edited: Tab (and
 * Shift+Tab) moves between the fields that count, Enter applies, Esc
 * cancels, digits and dots type.  Returns 1 when the key was used.
 */
int
se_wired_key(
	struct se_app *app,
	const struct se_event *event)
{
	struct se_wired *wired;
	int step;
	int tries;
	int counts;
	int used;

	/* Nothing edited: the keys are the window's. */
	wired = &app->wired;
	if (wired->interface[0] == '\0')
		return 0;

	/* Tab and Shift+Tab, over the fields that count. */
	if (event->key == SE_KEY_TAB) {
		step = 1;
		if ((event->modifiers & SE_MOD_SHIFT) != 0U)
			step = SE_WIRED_FIELDS - 1;
		for (tries = 0; tries < SE_WIRED_FIELDS; tries++) {
			wired->focus = (wired->focus + step) % SE_WIRED_FIELDS;
			counts = wired_counts(wired, wired->focus);
			if (counts)
				break;
		}

		/* Taken. */
		app->dirty = 1;
		return 1;
	}

	/* Enter applies; Esc cancels. */
	if (event->key == SE_KEY_ENTER) {
		se_wired_apply(app);
		return 1;
	}

	/* Esc cancels. */
	if (event->key == SE_KEY_ESC) {
		se_wired_cancel(app);
		return 1;
	}

	/* Anything else types into the field with the keyboard, while no answer is awaited. */
	if (wired->asked)
		return 1;
	used = se_wired_type(wired, event);
	if (used == 0)
		return 0;
	if (wired->message_bad)
		wired->message[0] = '\0';
	app->dirty = 1;

	/* Succeeded: the field took the key. */
	return 1;
}

/* Tells whether a field counts: every one with a static address, only the DNS servers with DHCP. */
static int
wired_counts(
	const struct se_wired *wired,
	int index)
{
	/* With DHCP the address, mask and router are DHCP's. */
	if (wired->mode == KL_WIRED_DHCP && index < SE_WIRED_DNS1)
		return 0;

	/* Succeeded: the field counts. */
	return 1;
}

/* Draws one field's row: its label, the box, the text or what it is for, and the cursor in the field with the keyboard. */
static void
wired_field_draw(
	struct se_app *app,
	struct fm_canvas *canvas,
	int index,
	int x,
	int y,
	int width)
{
	const struct se_wired *wired;
	const struct se_field *field;
	struct fm_rect box;
	const char *text;
	fm_color ink;
	fm_color label;
	int counts;
	int right;

	/* The label, faint for a field that does not count. */
	wired = &app->wired;
	field = &wired->fields[index];
	counts = wired_counts(wired, index);
	label = SE_COLOR_TEXT;
	if (!counts)
		label = SE_COLOR_TEXT_FAINT;
	(void)fm_text_draw_fit(app->text, canvas, x + WIRED_PAD, fm_text_center(WIRED_TEXT_ROW, y + 8, 36), wired_labels[index], WIRED_TEXT_ROW, 0, WIRED_FIELD_X - 30, label);

	/* The box: white, the accent's edge with the keyboard. */
	box.x = x + WIRED_FIELD_X;
	box.y = y + 8;
	box.width = width - WIRED_FIELD_X - WIRED_PAD;
	box.height = 36;
	if (counts) {
		fm_canvas_round(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, SE_COLOR_FIELD);
	} else {
		fm_canvas_round(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, SE_COLOR_SEPARATOR);
	}

	/* Its edge. */
	if (counts && wired->focus == index) {
		fm_canvas_round_border(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, 1.5f, SE_COLOR_ACCENT);
	} else {
		fm_canvas_round_border(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, 1.0f, SE_COLOR_SEPARATOR);
	}

	/* A click on it gives it the keyboard. */
	se_ui_hit(app, &box, SE_HIT_CONTROL, WIRED_FIELD_FIRST + index);

	/* The text, or what an empty field is for ("From DHCP" for a field that does not count). */
	text = field->text;
	ink = SE_COLOR_TEXT;
	if (!counts) {
		text = "From DHCP";
		ink = SE_COLOR_TEXT_FAINT;
	} else if (field->length == 0) {
		text = wired_placeholders[index];
		ink = SE_COLOR_TEXT_FAINT;
	}

	/* The text inside the box, and the cursor after it in the field with the keyboard. */
	fm_canvas_clip_push(canvas, &box);
	right = box.x + 12 + fm_text_draw(app->text, canvas, box.x + 12, fm_text_center(WIRED_TEXT_ROW, box.y, box.height), text, strlen(text), WIRED_TEXT_ROW, 0, ink);
	if (field->length == 0 || !counts)
		right = box.x + 12;
	if (counts && wired->focus == index)
		fm_canvas_line(canvas, (float)right + 1.5f, (float)box.y + 9.0f, (float)right + 1.5f, (float)(box.y + box.height) - 9.0f, 1.5f, SE_COLOR_ACCENT);
	fm_canvas_clip_pop(canvas);
}

/* Says how a wired interface is configured, in words. */
static const char *
wired_mode_text(
	unsigned mode)
{
	/* Each mode. */
	switch (mode) {
	case KL_WIRED_DHCP:
		return "DHCP";
	case KL_WIRED_STATIC:
		return "Static address";
	default:
		break;
	}

	/* Not known (disabled, or a desktop that does not say). */
	return "Not known";
}
