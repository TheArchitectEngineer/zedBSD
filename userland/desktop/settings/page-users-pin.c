/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Users page's PIN card (ws163-p003, ws172-p002): the six-digit PIN
 * that logs in and unlocks on this machine's screen.
 *
 * The card says whether a PIN is set, as the desktop tells it
 * (kl_system_account_enrolled: the session manager keeps the PIN, in
 * /etc/passkey on zedBSD, which Settings cannot read), and has three
 * fields: the current password, the new PIN and the new PIN again (digits
 * only, at most six, shown as dots).  Set Up PIN (Change PIN when one is
 * set) asks the desktop with the password and the PIN; Remove PIN asks
 * with the password alone (libkeiland's kl_system_account_set_pin, an
 * empty PIN).  The session manager checks the password and sets or
 * removes the PIN.  The fields are wiped as soon as the change is asked,
 * and the answer comes as a line under the buttons.  A desktop without the
 * PIN (KL_SYSTEM_HAS_PIN) says so and offers no change.
 */

#include "settings.h"

#include <keiland/keiland.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The card's controls: its three fields, Set Up PIN (or Change PIN), and Remove PIN. */
#define PIN_FIELD_FIRST		200
#define PIN_APPLY		210
#define PIN_REMOVE		211

/* The fields: the current password, the new PIN, and the new PIN again. */
#define PIN_PASSWORD		0
#define PIN_NEW			1
#define PIN_AGAIN		2

/* The digits of a PIN. */
#define PIN_DIGITS		6U

/* A field's row, the field's left edge in the row, and the line under the buttons. */
#define PIN_ROW			52
#define PIN_FIELD_X		210
#define PIN_MESSAGE_LINE	30

/* The text sizes of a row and of a message. */
#define PIN_TEXT_ROW		15U
#define PIN_TEXT_SUB		13U

/* The room of a refusal's word. */
#define PIN_REASON		32U

/* The fields' labels and placeholders. */
static const char *const pin_labels[SE_PIN_FIELDS] = { "Current password", "New PIN", "New PIN again" };
static const char *const pin_placeholders[SE_PIN_FIELDS] = { "Your password now", "Six digits", "The same again" };

static int pin_available(const struct se_app *app);
static int pin_is_set(const struct se_app *app, int *set);
static int pin_ready(const struct se_app *app);
static int pin_remove_ready(const struct se_app *app);
static void pin_ask(struct se_app *app, int removing);
static void pin_field_draw(struct se_app *app, struct kl_canvas *canvas, int index, int x, int y, int width);

/*
 * Draws the PIN card from a top edge; returns the edge below it.
 */
int
se_users_pin_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int top,
	int width)
{
	struct se_users *users;
	const char *apply;
	const char *state;
	kl_color ink;
	int available;
	int enabled;
	int removable;
	int known;
	int differs;
	int apply_width;
	int remove_width;
	int right;
	int height;
	int index;
	int y;

	/* Without the PIN, a short card that says so. */
	users = &app->users;
	available = pin_available(app);
	height = 64 + 36 + SE_PIN_FIELDS * PIN_ROW + 60 + PIN_MESSAGE_LINE;
	if (!available)
		height = 64 + 50;
	y = se_card_begin(app, canvas, x, top, width, height, "PIN", "Unlock the locked screen with six digits.");
	if (!available) {
		(void)kl_text_draw_fit(app->text, canvas, x + 20, y + 24, "This desktop cannot set a PIN here.", PIN_TEXT_ROW, 0, width - 40, SE_COLOR_TEXT_SECONDARY);
		return top + height;
	}

	/* Whether a PIN is set now, once the desktop has told it. */
	known = pin_is_set(app, &users->pin_set);
	state = "Looking for your PIN...";
	if (known && !users->pin_set)
		state = "No PIN is set. The login and locked screens take your password.";
	if (known && users->pin_set)
		state = "A PIN is set. After a restart, log in once with your password.";
	(void)kl_text_draw_fit(app->text, canvas, x + 20, y + 22, state, PIN_TEXT_SUB, 0, width - 40, SE_COLOR_TEXT_SECONDARY);
	y += 36;

	/* Each field. */
	for (index = 0; index < SE_PIN_FIELDS; index++) {
		pin_field_draw(app, canvas, index, x, y, width);
		y += PIN_ROW;
	}

	/* The buttons at the right: Set Up PIN (Change PIN), and Remove PIN left of it when one is set. */
	right = x + width - 20;
	apply = "Set Up PIN";
	if (users->pin_set)
		apply = "Change PIN";
	apply_width = se_button_width(app, apply);
	enabled = pin_ready(app);
	(void)se_button_draw(app, canvas, right - apply_width, y + 12, apply, 1, enabled, PIN_APPLY);
	if (users->pin_set) {
		remove_width = se_button_width(app, "Remove PIN");
		removable = pin_remove_ready(app);
		(void)se_button_draw(app, canvas, right - apply_width - 8 - remove_width, y + 12, "Remove PIN", 0, removable, PIN_REMOVE);
	}

	/* The last answer (green when it was done, red when it failed). */
	y += 60;
	ink = SE_COLOR_GOOD;
	if (users->pin_bad)
		ink = SE_COLOR_BAD;
	if (users->pin_message[0] != '\0')
		(void)kl_text_draw_fit(app->text, canvas, x + 20, y + 18, users->pin_message, PIN_TEXT_SUB, 0, width - 40, ink);

	/* Without an answer to show, what the fields still need: the new PIN the same twice. */
	differs = 0;
	if (users->pin_message[0] == '\0' && users->pin_fields[PIN_NEW].length != 0 && users->pin_fields[PIN_AGAIN].length != 0)
		differs = strcmp(users->pin_fields[PIN_NEW].text, users->pin_fields[PIN_AGAIN].text);
	if (differs != 0)
		(void)kl_text_draw_fit(app->text, canvas, x + 20, y + 18, "The new PIN and its repeat differ.", PIN_TEXT_SUB, 0, width - 40, SE_COLOR_TEXT_SECONDARY);

	/* The edge below the card. */
	return top + height;
}

/*
 * Carries out a click on a control of the PIN card.  Returns 1 when the
 * control was the card's.
 */
int
se_users_pin_press(
	struct se_app *app,
	int index)
{
	struct se_users *users;
	int ready;

	/* A field takes the keyboard. */
	users = &app->users;
	if (index >= PIN_FIELD_FIRST && index < PIN_FIELD_FIRST + SE_PIN_FIELDS) {
		users->pin_focus = index - PIN_FIELD_FIRST;
		users->keyboard = SE_USERS_KEYBOARD_PIN;
		return 1;
	}

	/* Set Up PIN or Change PIN, when the fields are ready. */
	if (index == PIN_APPLY) {
		ready = pin_ready(app);
		if (ready)
			pin_ask(app, 0);
		return 1;
	}

	/* Remove PIN, when the password is typed. */
	if (index == PIN_REMOVE) {
		ready = pin_remove_ready(app);
		if (ready)
			pin_ask(app, 1);
		return 1;
	}

	/* Not the card's. */
	return 0;
}

/*
 * Takes a key while the PIN card's fields have the keyboard: Tab moves
 * between them, Enter asks the change when the fields are ready and
 * otherwise goes to the next field, Esc empties the fields (or gives the
 * keyboard back to the password card), the others type (only digits into
 * the PIN's fields, at most six).  Returns 1 when the key was used.
 */
int
se_users_pin_key(
	struct se_app *app,
	const struct se_event *event)
{
	struct se_users *users;
	struct kl_field *field;
	uint32_t character;
	int ready;
	int used;

	/* Not the card's keyboard. */
	users = &app->users;
	if (users->keyboard != SE_USERS_KEYBOARD_PIN)
		return 0;

	/* Tab and Shift+Tab. */
	if (event->key == SE_KEY_TAB) {
		if ((event->modifiers & SE_MOD_SHIFT) != 0U) {
			users->pin_focus = (users->pin_focus + SE_PIN_FIELDS - 1) % SE_PIN_FIELDS;
		} else {
			users->pin_focus = (users->pin_focus + 1) % SE_PIN_FIELDS;
		}

		/* Taken. */
		return 1;
	}

	/* Enter: the change, or the next field. */
	if (event->key == SE_KEY_ENTER) {
		ready = pin_ready(app);
		if (ready) {
			pin_ask(app, 0);
		} else {
			users->pin_focus = (users->pin_focus + 1) % SE_PIN_FIELDS;
		}

		/* Taken. */
		return 1;
	}

	/* Esc empties the fields; with nothing typed the keyboard goes back to the password card. */
	if (event->key == SE_KEY_ESC) {
		if (users->pin_fields[PIN_PASSWORD].length == 0 && users->pin_fields[PIN_NEW].length == 0 && users->pin_fields[PIN_AGAIN].length == 0)
			users->keyboard = SE_USERS_KEYBOARD_PASSWORD;
		se_users_pin_wipe(users);
		users->pin_message[0] = '\0';
		return 1;
	}

	/*
	 * The PIN's fields keep digits only, six at most: a character that is
	 * not a digit, or one more than six (without a selection it would
	 * replace), is taken and dropped before the field sees it (BUG-257:
	 * taking it back from the field's end left the caret past the text,
	 * and the next key wrote outside it).
	 */
	field = &users->pin_fields[users->pin_focus];
	character = kl_key_character(event->key, event->modifiers);
	if (users->pin_focus != PIN_PASSWORD && character != 0U && (event->modifiers & SE_MOD_CTRL) == 0U) {
		if (character < '0' || character > '9')
			return 1;
		if (field->length >= PIN_DIGITS && field->caret == field->anchor)
			return 1;
	}

	/* Anything else types into the field with the keyboard. */
	used = se_field_key(field, event);
	if (used == 0)
		return 0;

	/* A new character takes the last answer away. */
	if (!users->pin_asked)
		users->pin_message[0] = '\0';

	/* Succeeded: the field took the key. */
	return 1;
}

/*
 * Takes the answer of a PIN's change when it is the card's.  Returns 1
 * when the request was the card's.
 */
int
se_users_pin_result(
	struct se_app *app,
	uint32_t request,
	int error)
{
	struct se_users *users;
	const char *message;
	char reason[PIN_REASON];
	int refused;
	int locked;
	int bad;

	/* Only the change the card asked. */
	users = &app->users;
	if (!users->pin_asked || request != users->pin_request)
		return 0;
	users->pin_asked = 0;

	/* What the answer says. */
	bad = 1;
	switch (error) {
	case 0:
		message = "The PIN is set. The login and locked screens take it from now on.";
		if (users->pin_removing)
			message = "The PIN is removed.";
		bad = 0;
		break;
	case EPERM:
		/* The refusal's word, when the desktop gave one: a locked account, or a wrong password. */
		refused = kl_system_account_refusal(app->system, request, reason, sizeof(reason));
		locked = 1;
		if (refused)
			locked = strcmp(reason, "locked");
		message = "The current password is wrong.";
		if (locked == 0)
			message = "Your account is locked: it cannot have a PIN.";
		users->pin_focus = PIN_PASSWORD;
		break;
	case EINVAL:
		message = "The PIN is not accepted: use six digits.";
		break;
	case ENOTSUP:
		message = "This desktop cannot set a PIN.";
		break;
	case EBUSY:
		message = "Another check is under way. Try again in a moment.";
		break;
	default:
		message = "The PIN could not be changed.";
		break;
	}

	/* Shown under the buttons, and logged for the tests (without the password or the PIN). */
	(void)snprintf(users->pin_message, sizeof(users->pin_message), "%s", message);
	users->pin_bad = bad;
	app->dirty = 1;
	se_log("USERS pin result request=%u errno=%d remove=%d", request, error, users->pin_removing);

	/* Succeeded: the answer was the card's. */
	return 1;
}

/*
 * Wipes the PIN card's fields.
 */
void
se_users_pin_wipe(
	struct se_users *users)
{
	int index;

	/* Each field (se_field_clear overwrites its text). */
	for (index = 0; index < SE_PIN_FIELDS; index++)
		se_field_clear(&users->pin_fields[index]);
}

/* Tells whether the desktop offers the PIN (KL_SYSTEM_HAS_PIN). */
static int
pin_available(
	const struct se_app *app)
{
	unsigned bits;

	/* No desktop system. */
	if (app->system == NULL)
		return 0;

	/* Its PIN. */
	bits = kl_system_capabilities(app->system);
	if ((bits & KL_SYSTEM_HAS_PIN) == 0U)
		return 0;

	/* Offered. */
	return 1;
}

/* Tells whether the desktop has told whether a PIN is set (1), and sets *set to whether one is. */
static int
pin_is_set(
	const struct se_app *app,
	int *set)
{
	unsigned pin;
	unsigned keys;
	int known;

	/* Not known before the desktop told it. */
	*set = 0;
	known = kl_system_account_enrolled(app->system, &pin, &keys);
	if (!known)
		return 0;

	/* Succeeded: known, and whether a PIN is set. */
	if (pin != 0U)
		*set = 1;
	return 1;
}

/* Tells whether the fields are ready to set the PIN: the password typed, six digits twice the same, and none asked. */
static int
pin_ready(
	const struct se_app *app)
{
	const struct se_users *users;
	int same;

	/* One change at a time. */
	users = &app->users;
	if (users->pin_asked)
		return 0;

	/* The password typed, and the new PIN whole. */
	if (users->pin_fields[PIN_PASSWORD].length == 0)
		return 0;
	if (users->pin_fields[PIN_NEW].length != PIN_DIGITS)
		return 0;

	/* The new PIN twice the same. */
	same = strcmp(users->pin_fields[PIN_NEW].text, users->pin_fields[PIN_AGAIN].text);
	if (same != 0)
		return 0;

	/* Ready. */
	return 1;
}

/* Tells whether the PIN can be removed: one is set, the password typed, and none asked. */
static int
pin_remove_ready(
	const struct se_app *app)
{
	const struct se_users *users;

	/* One change at a time, of a PIN that is set. */
	users = &app->users;
	if (users->pin_asked || !users->pin_set)
		return 0;

	/* The password typed. */
	if (users->pin_fields[PIN_PASSWORD].length == 0)
		return 0;

	/* Ready. */
	return 1;
}

/* Asks the desktop to set the PIN (or to remove it), and wipes the fields. */
static void
pin_ask(
	struct se_app *app,
	int removing)
{
	struct se_users *users;
	const char *pin;
	uint32_t request;
	int error;

	/* Asked; the password and the PIN leave with the next flush. */
	users = &app->users;
	pin = users->pin_fields[PIN_NEW].text;
	if (removing)
		pin = "";
	error = kl_system_account_set_pin(app->system, users->pin_fields[PIN_PASSWORD].text, pin, &request);
	se_users_pin_wipe(users);
	users->pin_focus = PIN_PASSWORD;

	/* Not asked: said at once. */
	if (error != 0) {
		(void)snprintf(users->pin_message, sizeof(users->pin_message), "The PIN could not be changed (%s).", strerror(error));
		users->pin_bad = 1;
		se_log("USERS pin errno=%d", error);
		return;
	}

	/* Succeeded: the answer comes as a result. */
	users->pin_asked = 1;
	users->pin_removing = removing;
	users->pin_request = request;
	(void)snprintf(users->pin_message, sizeof(users->pin_message), "Checking the password...");
	users->pin_bad = 0;
	se_log("USERS pin request=%u remove=%d", request, removing);
}

/* Draws one field's row: its label at the left, the field at the right, dots or the placeholder, and the cursor in the field with the keyboard. */
static void
pin_field_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int index,
	int x,
	int y,
	int width)
{
	struct se_users *users;
	struct kl_rect box;
	int focused;

	/* The label. */
	users = &app->users;
	(void)kl_text_draw_fit(app->text, canvas, x + 20, kl_text_center(PIN_TEXT_ROW, y + 8, 36), pin_labels[index], PIN_TEXT_ROW, 0, PIN_FIELD_X - 30, SE_COLOR_TEXT);

	/* The field's place, and whether it has the keyboard. */
	box.x = x + PIN_FIELD_X;
	box.y = y + 8;
	box.width = width - PIN_FIELD_X - 20;
	box.height = 36;
	focused = 0;
	if (users->keyboard == SE_USERS_KEYBOARD_PIN && users->pin_focus == index)
		focused = 1;

	/* A click on it gives it the keyboard. */
	se_ui_hit(app, &box, SE_HIT_CONTROL, PIN_FIELD_FIRST + index);

	/* libkeiland's field: all three are secrets, as dots and without an input method (ws090-p007). */
	(void)se_field_draw(app, canvas, &users->pin_fields[index], &box, pin_placeholders[index], SE_FIELD_SECRET, focused);
}
