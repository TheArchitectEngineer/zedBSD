/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Users page (ws160-p002; the 2026-10-05 user request "パスワードは
 * SettingsのUsers画面でもGUI実装します。"): the account of the user Settings
 * runs as, and the change of its password.
 *
 * The account card shows the user's name, full name and home (the
 * passwd database, read once).  The password card has three fields, the
 * current password, the new one and the new one again, shown as dots
 * unless Show is on; the field with the keyboard has the accent's edge,
 * Tab and a click move between them.  Change Password (and Enter) asks the
 * desktop (libkeiland's kl_system_account_set_password); Settings holds
 * no privilege and never runs the system's programs itself: the
 * compositor changes the password through libkeiland-backend (on zedBSD,
 * passwd).  The fields are wiped as soon as the change is asked, and the
 * answer comes as a line under the buttons: changed, the current password
 * wrong, the new one refused (at least 8 characters, not the old one), or
 * the desktop cannot change it.  A desktop without the account
 * (KL_SYSTEM_HAS_ACCOUNT) says so and offers no change.
 */

#include "settings.h"

#include <keiland.h>

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The controls of the page: the three fields, Show, and Change Password. */
#define USERS_FIELD_FIRST	1
#define USERS_SHOW		10
#define USERS_CHANGE		11

/* A field's row, the field's left edge in the row, and the line under the buttons. */
#define USERS_ROW		52
#define USERS_FIELD_X		210
#define USERS_MESSAGE_LINE	30

/* The text sizes of a row and of a message. */
#define USERS_TEXT_ROW		15U
#define USERS_TEXT_SUB		13U

/* The space between two cards. */
#define USERS_GAP		16

/* The fields' labels and placeholders. */
static const char *const users_labels[SE_USERS_FIELDS] = { "Current password", "New password", "New password again" };
static const char *const users_placeholders[SE_USERS_FIELDS] = { "Your password now", "At least 8 characters", "The same again" };

static int users_available(const struct se_app *app);
static void users_read(struct se_users *users);
static int users_ready(const struct se_app *app);
static void users_change(struct se_app *app);
static void users_wipe(struct se_users *users);
static void users_field_draw(struct se_app *app, struct fm_canvas *canvas, int index, int x, int y, int width);

/*
 * Draws the Users page's cards from a top edge; returns the edge below
 * them.
 */
int
se_users_draw(
	struct se_app *app,
	struct fm_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct se_users *users;
	const char *reveal;
	fm_color ink;
	int available;
	int enabled;
	int differs;
	int change;
	int show;
	int right;
	int height;
	int index;
	int y;

	/* The account, read once. */
	users = &app->users;
	if (!users->read)
		users_read(users);

	/* The account card: the name, the full name and the home. */
	height = se_card_height(3, 1);
	y = se_card_begin(app, canvas, x, top, width, height, "Your account", NULL);
	y = se_row_value(app, canvas, x, y, width, "User name", users->name, 0);
	y = se_row_value(app, canvas, x, y, width, "Full name", users->full_name, 0);
	(void)se_row_value(app, canvas, x, y, width, "Home", users->home, 1);
	top += height + USERS_GAP;

	/* The password card: the three fields, the buttons and the message. */
	available = users_available(app);
	height = 64 + SE_USERS_FIELDS * USERS_ROW + 60 + USERS_MESSAGE_LINE;
	if (!available)
		height = 64 + 50;
	y = se_card_begin(app, canvas, x, top, width, height, "Password", "Change the password you log in with.");
	if (!available) {
		(void)fm_text_draw_fit(app->text, canvas, x + 20, y + 24, "This desktop cannot change the password here.", USERS_TEXT_ROW, 0, width - 40, SE_COLOR_TEXT_SECONDARY);
		return top + height;
	}

	/* Each field. */
	for (index = 0; index < SE_USERS_FIELDS; index++) {
		users_field_draw(app, canvas, index, x, y, width);
		y += USERS_ROW;
	}

	/* The buttons at the right: Change Password, and Show or Hide left of it. */
	right = x + width - 20;
	reveal = "Show";
	if (users->shown)
		reveal = "Hide";
	change = se_button_width(app, "Change Password");
	show = se_button_width(app, reveal);
	enabled = users_ready(app);
	(void)se_button_draw(app, canvas, right - change, y + 12, "Change Password", 1, enabled, USERS_CHANGE);
	(void)se_button_draw(app, canvas, right - change - 8 - show, y + 12, reveal, 0, 1, USERS_SHOW);

	/* What the fields still need, or the last answer (green when it changed, red when it failed). */
	y += 60;
	ink = SE_COLOR_TEXT_SECONDARY;
	if (users->message[0] != '\0' && users->message_bad)
		ink = SE_COLOR_BAD;
	if (users->message[0] != '\0' && !users->message_bad)
		ink = SE_COLOR_GOOD;
	if (users->message[0] != '\0')
		(void)fm_text_draw_fit(app->text, canvas, x + 20, y + 18, users->message, USERS_TEXT_SUB, 0, width - 40, ink);

	/* Without an answer to show, what the fields still need: the new password the same twice. */
	differs = 0;
	if (users->message[0] == '\0' && users->fields[1].length != 0 && users->fields[2].length != 0)
		differs = strcmp(users->fields[1].text, users->fields[2].text);
	if (differs != 0)
		(void)fm_text_draw_fit(app->text, canvas, x + 20, y + 18, "The new password and its repeat differ.", USERS_TEXT_SUB, 0, width - 40, SE_COLOR_TEXT_SECONDARY);

	/* The edge below the card. */
	return top + height;
}

/*
 * Carries out a click on a control of the Users page.
 */
void
se_users_press(
	struct se_app *app,
	int index)
{
	struct se_users *users;
	int ready;

	/* A field takes the keyboard. */
	users = &app->users;
	if (index >= USERS_FIELD_FIRST && index < USERS_FIELD_FIRST + SE_USERS_FIELDS) {
		users->focus = index - USERS_FIELD_FIRST;
		return;
	}

	/* Show and Hide. */
	if (index == USERS_SHOW) {
		users->shown = !users->shown;
		return;
	}

	/* Change Password, when the fields are ready. */
	if (index == USERS_CHANGE) {
		ready = users_ready(app);
		if (ready)
			users_change(app);
	}
}

/*
 * Takes a key on the Users page: Tab (and Shift+Tab) moves between the
 * fields, Enter asks the change when the fields are ready and otherwise
 * goes to the next field, Esc empties the fields (or, when they are empty,
 * is the window's), and the others type.  Returns 1 when the key was used.
 */
int
se_users_key(
	struct se_app *app,
	const struct se_event *event)
{
	struct se_users *users;
	int available;
	int ready;
	int used;

	/* Without the account there is nothing to type. */
	users = &app->users;
	available = users_available(app);
	if (!available)
		return 0;

	/* Tab and Shift+Tab. */
	if (event->key == SE_KEY_TAB) {
		if ((event->modifiers & SE_MOD_SHIFT) != 0U) {
			users->focus = (users->focus + SE_USERS_FIELDS - 1) % SE_USERS_FIELDS;
		} else {
			users->focus = (users->focus + 1) % SE_USERS_FIELDS;
		}

		/* Taken. */
		return 1;
	}

	/* Enter: the change, or the next field. */
	if (event->key == SE_KEY_ENTER) {
		ready = users_ready(app);
		if (ready) {
			users_change(app);
		} else {
			users->focus = (users->focus + 1) % SE_USERS_FIELDS;
		}

		/* Taken. */
		return 1;
	}

	/* Esc empties the fields; with nothing typed it is the window's (back). */
	if (event->key == SE_KEY_ESC) {
		if (users->fields[0].length == 0 && users->fields[1].length == 0 && users->fields[2].length == 0)
			return 0;
		users_wipe(users);
		users->message[0] = '\0';
		return 1;
	}

	/* Anything else types into the field with the keyboard (or is not the page's). */
	used = se_field_key(&users->fields[users->focus], event);
	if (used == 0)
		return 0;

	/* A new character takes the last answer away. */
	if (!users->asked)
		users->message[0] = '\0';

	/* Succeeded: the field took the key. */
	return 1;
}

/*
 * Takes the answer of the password change when it is the Users page's.
 * Returns 1 when the request was the page's.
 */
int
se_users_result(
	struct se_app *app,
	uint32_t request,
	int error)
{
	struct se_users *users;
	const char *message;
	int bad;

	/* Only the change the page asked. */
	users = &app->users;
	if (!users->asked || request != users->request)
		return 0;
	users->asked = 0;

	/* What the answer says. */
	bad = 1;
	switch (error) {
	case 0:
		message = "Your password is changed. Use the new one from now on.";
		bad = 0;
		break;
	case EPERM:
		message = "The current password is wrong.";
		users->focus = 0;
		break;
	case EINVAL:
		message = "The new password is not accepted: use at least 8 characters, not the old password.";
		users->focus = 1;
		break;
	case ENOTSUP:
		message = "This desktop cannot change the password.";
		break;
	case EBUSY:
		message = "Another change of the password is under way. Try again in a moment.";
		break;
	default:
		message = "The password could not be changed.";
		break;
	}

	/* Shown under the buttons, and logged for the tests (without any password). */
	(void)snprintf(users->message, sizeof(users->message), "%s", message);
	users->message_bad = bad;
	app->dirty = 1;
	se_log("USERS result request=%u errno=%d", request, error);

	/* Succeeded: the answer was the page's. */
	return 1;
}

/* Tells whether the desktop offers the account (KL_SYSTEM_HAS_ACCOUNT). */
static int
users_available(
	const struct se_app *app)
{
	unsigned bits;

	/* No desktop system. */
	if (app->system == NULL)
		return 0;

	/* Its account. */
	bits = kl_system_capabilities(app->system);
	if ((bits & KL_SYSTEM_HAS_ACCOUNT) == 0U)
		return 0;

	/* Succeeded: offered. */
	return 1;
}

/* Wipes the password fields at the window's end. */
void
se_users_close(
	struct se_app *app)
{
	/* The three fields. */
	users_wipe(&app->users);
}

/* Reads the account of the user Settings runs as. */
static void
users_read(
	struct se_users *users)
{
	struct passwd *account;
	size_t length;

	/* The passwd entry of the real user ID. */
	users->read = 1;
	account = getpwuid(getuid());
	if (account == NULL)
		return;

	/* The name, the full name (the first field of the comment), and the home. */
	(void)snprintf(users->name, sizeof(users->name), "%s", account->pw_name);
	(void)snprintf(users->full_name, sizeof(users->full_name), "%s", account->pw_gecos);
	length = strcspn(users->full_name, ",");
	users->full_name[length] = '\0';
	(void)snprintf(users->home, sizeof(users->home), "%s", account->pw_dir);
	se_log("USERS account name=%s", users->name);
}

/* Tells whether the fields are ready for the change: all typed, the new one twice the same, and none asked yet. */
static int
users_ready(
	const struct se_app *app)
{
	const struct se_users *users;
	int same;

	/* One change at a time. */
	users = &app->users;
	if (users->asked)
		return 0;

	/* All three typed. */
	if (users->fields[0].length == 0 || users->fields[1].length == 0 || users->fields[2].length == 0)
		return 0;

	/* The new one twice the same. */
	same = strcmp(users->fields[1].text, users->fields[2].text);
	if (same != 0)
		return 0;

	/* Succeeded: ready. */
	return 1;
}

/* Asks the desktop for the change, and wipes the fields. */
static void
users_change(
	struct se_app *app)
{
	struct se_users *users;
	uint32_t request;
	int error;

	/* Asked; the passwords leave with the next flush. */
	users = &app->users;
	error = kl_system_account_set_password(app->system, users->fields[0].text, users->fields[1].text, &request);
	users_wipe(users);
	users->focus = 0;

	/* Not asked: said at once. */
	if (error != 0) {
		(void)snprintf(users->message, sizeof(users->message), "The password could not be changed (%s).", strerror(error));
		users->message_bad = 1;
		se_log("USERS change errno=%d", error);
		return;
	}

	/* Succeeded: the answer comes as a result. */
	users->asked = 1;
	users->request = request;
	(void)snprintf(users->message, sizeof(users->message), "Changing the password...");
	users->message_bad = 0;
	se_log("USERS change request=%u", request);
}

/* Wipes the three fields. */
static void
users_wipe(
	struct se_users *users)
{
	int index;

	/* Each field (se_field_clear overwrites its text). */
	for (index = 0; index < SE_USERS_FIELDS; index++)
		se_field_clear(&users->fields[index]);
}

/* Draws one field's row: its label at the left, the field at the right, dots or the text, and the cursor in the field with the keyboard. */
static void
users_field_draw(
	struct se_app *app,
	struct fm_canvas *canvas,
	int index,
	int x,
	int y,
	int width)
{
	const struct se_users *users;
	const struct se_field *field;
	struct fm_rect box;
	char dots[SE_KEY_TEXT];
	const char *text;
	fm_color ink;
	size_t count;
	int right;

	/* The label. */
	users = &app->users;
	field = &users->fields[index];
	(void)fm_text_draw_fit(app->text, canvas, x + 20, fm_text_center(USERS_TEXT_ROW, y + 8, 36), users_labels[index], USERS_TEXT_ROW, 0, USERS_FIELD_X - 30, SE_COLOR_TEXT);

	/* The field: white, the accent's edge when it has the keyboard. */
	box.x = x + USERS_FIELD_X;
	box.y = y + 8;
	box.width = width - USERS_FIELD_X - 20;
	box.height = 36;
	fm_canvas_round(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, FM_RGB(0xffffff));
	if (users->focus == index) {
		fm_canvas_round_border(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, 1.5f, SE_COLOR_ACCENT);
	} else {
		fm_canvas_round_border(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, 8.0f, 1.0f, SE_COLOR_SEPARATOR);
	}

	/* A click on it gives it the keyboard. */
	se_ui_hit(app, &box, SE_HIT_CONTROL, USERS_FIELD_FIRST + index);

	/* Dots, or the text when shown, or the placeholder. */
	text = field->text;
	if (!users->shown) {
		for (count = 0; count < field->length && count + 1U < sizeof(dots); count++)
			dots[count] = '*';
		dots[count] = '\0';
		text = dots;
	}

	/* An empty field shows what it is for. */
	ink = SE_COLOR_TEXT;
	if (field->length == 0) {
		text = users_placeholders[index];
		ink = SE_COLOR_TEXT_FAINT;
	}

	/* The text inside the field, and the cursor after it in the field with the keyboard. */
	fm_canvas_clip_push(canvas, &box);
	right = box.x + 12 + fm_text_draw(app->text, canvas, box.x + 12, fm_text_center(USERS_TEXT_ROW, box.y, box.height), text, strlen(text), USERS_TEXT_ROW, 0, ink);
	if (field->length == 0)
		right = box.x + 12;
	if (users->focus == index)
		fm_canvas_line(canvas, (float)right + 1.5f, (float)box.y + 9.0f, (float)right + 1.5f, (float)(box.y + box.height) - 9.0f, 1.5f, SE_COLOR_ACCENT);
	fm_canvas_clip_pop(canvas);
	memset(dots, 0, sizeof(dots));
}
