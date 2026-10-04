/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The editing operations and the previous application (ws102-p017,
 * plan/ws102/design.md section 2.10), the compositor's side of the
 * on-screen keyboard's tool face (its buttons come with ws102-p016).
 *
 * keiland_edit_v1: a window that asks for it (keiland_edit_manager_v1.get_edit;
 * libkeiland's windows do) tells which editing operations it carries out and
 * its state -- whether it has a selection, something to paste, something
 * to undo or redo, and whether a selection is being made -- and hears the
 * operations as actions.  zwl_edit_action sends an operation to the
 * focused window: as an action to a window with the protocol, otherwise as
 * the keys it stands for (Ctrl+C, X, V, Z, Y, A; Ctrl+Shift+C and V for a
 * terminal, by its application ID).  zwl_edit_state reads which operations
 * the focused window can take now, for the buttons' grey.
 *
 * zwl_focus_previous brings the window focused before the one on top
 * forward (the second in the stacking order, which a raise and the focus
 * follow): twice goes back.  The on-screen keyboard stays open.
 *
 * Until the tool face exists, Super+Alt with C, X, V, Z, Y, A, S (select
 * begin), E (select end) and P (the previous application) call these, and
 * Super+Alt+Q logs what zwl_edit_state reads; Super+Alt+H logs the
 * clipboard's history and Super+Alt+1 ... 0 pastes its items 1 ... 10
 * (clipboard.c; the tests use them).
 */

#include "edit.h"
#include "desktop.h"
#include "menu.h"
#include "data.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The requests of keiland_edit_manager_v1 and of keiland_edit_v1, and the event. */
#define EDIT_MANAGER_DESTROY		0U
#define EDIT_MANAGER_GET_EDIT		1U
#define EDIT_DESTROY			0U
#define EDIT_SET_STATE			1U
#define EDIT_EVENT_ACTION		0U

/* The modifiers' bits the keys are sent with: Shift, Control, Alt and Super. */
#define EDIT_SHIFT			1U
#define EDIT_CONTROL			4U
#define EDIT_ALT			8U
#define EDIT_SUPER			0x40U

/* The evdev codes of the keys that stand for the operations, and of the shortcuts. */
#define EDIT_KEY_A			30U
#define EDIT_KEY_C			46U
#define EDIT_KEY_E			18U
#define EDIT_KEY_P			25U
#define EDIT_KEY_Q			16U
#define EDIT_KEY_H			35U
#define EDIT_KEY_1			2U
#define EDIT_KEY_0			11U
#define EDIT_KEY_S			31U
#define EDIT_KEY_V			47U
#define EDIT_KEY_X			45U
#define EDIT_KEY_Y			21U
#define EDIT_KEY_Z			44U

/*
 * The keys an operation stands for in a window without the protocol: the
 * key (0: none, the operation is not sent) and the modifiers, for most
 * windows and for a terminal (where Ctrl+C and Ctrl+V are the shell's).
 */
struct edit_keys {
	uint32_t key;
	uint32_t modifiers;
	uint32_t terminal_key;
	uint32_t terminal_modifiers;
};

/* Each operation's keys, in ZWL_EDIT_* order. */
static const struct edit_keys edit_keys[ZWL_EDIT_ACTIONS] = {
	{ EDIT_KEY_C, EDIT_CONTROL, EDIT_KEY_C, EDIT_CONTROL | EDIT_SHIFT },
	{ EDIT_KEY_X, EDIT_CONTROL, 0U, 0U },
	{ EDIT_KEY_V, EDIT_CONTROL, EDIT_KEY_V, EDIT_CONTROL | EDIT_SHIFT },
	{ EDIT_KEY_Z, EDIT_CONTROL, 0U, 0U },
	{ EDIT_KEY_Y, EDIT_CONTROL, 0U, 0U },
	{ EDIT_KEY_A, EDIT_CONTROL, 0U, 0U },
	{ 0U, 0U, 0U, 0U },
	{ 0U, 0U, 0U, 0U },
};

/* The operations' names, for the log. */
static const char *const edit_names[ZWL_EDIT_ACTIONS] = {
	"copy", "cut", "paste", "undo", "redo", "select_all", "select_begin", "select_end"
};

/* The application IDs whose Ctrl+C and Ctrl+V are the shell's (the terminal's keys are used). */
static const char *const edit_terminals[] = {
	"terminal",
	"zterm",
	"xterm",
};

/* The shortcuts that call the operations until the tool face exists (Super+Alt with the key). */
static const uint32_t edit_shortcut_keys[ZWL_EDIT_ACTIONS] = {
	EDIT_KEY_C, EDIT_KEY_X, EDIT_KEY_V, EDIT_KEY_Z, EDIT_KEY_Y, EDIT_KEY_A, EDIT_KEY_S, EDIT_KEY_E
};

static int edit_create(struct zwl_object *manager, const unsigned char *bytes, size_t size);
static struct zwl_object *edit_focused(struct zwl_server *server, struct zwl_object **window);
static int edit_terminal(const struct zwl_object *window);
static uint32_t edit_enabled(uint32_t actions, uint32_t flags);
static uint32_t edit_word(const unsigned char *bytes, size_t offset);

/*
 * Carries out a request of keiland_edit_manager_v1 or of a window's
 * keiland_edit_v1.
 */
int
zwl_edit_request(
	struct zwl_object *object,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	uint32_t actions;
	uint32_t flags;
	int error;

	/* The manager: it goes, or it gives a window its edit object. */
	if (object->kind == ZWL_EDIT_MANAGER) {
		if (opcode == EDIT_MANAGER_DESTROY && size == 0U) {
			zwl_object_destroy(object);
			return 0;
		}

		/* Only get_edit is left. */
		if (opcode != EDIT_MANAGER_GET_EDIT)
			return EPROTO;
		error = edit_create(object, bytes, size);
		if (error != 0)
			return error;
		return 0;
	}

	/* The edit object goes. */
	if (opcode == EDIT_DESTROY && size == 0U) {
		zwl_object_destroy(object);
		return 0;
	}

	/* Only set_state is left: the operations and the state, a word each. */
	if (opcode != EDIT_SET_STATE || size != 8U)
		return EPROTO;
	actions = edit_word(bytes, 0U) & ((1U << ZWL_EDIT_ACTIONS) - 1U);
	flags = edit_word(bytes, 4U) & ZWL_EDIT_FLAGS_ALL;

	/* A change is logged (the tests read it). */
	if (actions != object->edit_actions || flags != object->edit_flags) {
		object->edit_actions = actions;
		object->edit_flags = flags;
		printf("ZWL EDIT state client=%llu edit=%u actions=0x%x flags=0x%x\n", (unsigned long long)object->client->number, object->id, actions, flags);
	}

	/* Succeeded: the state is the window's now. */
	return 0;
}

/*
 * Forgets a window's toplevel that goes: its edit objects name nothing
 * from now on.
 */
void
zwl_edit_object_gone(
	struct zwl_object *object)
{
	struct zwl_object *other;

	/* Only a toplevel is named by an edit object. */
	if (object->kind != ZWL_TOPLEVEL)
		return;

	/* Each edit object of the client that names it. */
	for (other = object->client->objects; other != NULL; other = other->next) {
		if (other->kind == ZWL_EDIT && other->top == object)
			other->top = NULL;
	}
}

/*
 * Sends an editing operation (ZWL_EDIT_*) to the focused window: an action
 * to a window with the protocol that carries it out, otherwise the keys it
 * stands for.  Returns 0 when it was sent, ENOENT without a focused
 * window, ENOTSUP when the window cannot take it (no keys stand for it).
 */
int
zwl_edit_action(
	struct zwl_server *server,
	unsigned action)
{
	const struct edit_keys *keys;
	struct zwl_object *window;
	struct zwl_object *edit;
	uint32_t modifiers;
	uint32_t word;
	uint32_t time;
	uint32_t key;
	uint32_t with;
	int terminal;

	/* An operation there is. */
	if (action >= ZWL_EDIT_ACTIONS)
		return EINVAL;

	/* The focused window, and its edit object if it has one. */
	edit = edit_focused(server, &window);
	if (window == NULL) {
		printf("ZWL EDIT action=%s via=none reason=no-focus\n", edit_names[action]);
		return ENOENT;
	}

	/* A window with the protocol that carries the operation out hears it as an action. */
	if (edit != NULL && (edit->edit_actions & (1U << action)) != 0U) {
		word = action;
		(void)zwl_emit(edit->client, edit->id, EDIT_EVENT_ACTION, &word, sizeof(word));
		printf("ZWL EDIT action=%s via=protocol surface=%u app=%s\n", edit_names[action], window->id, window->app_id);
		return 0;
	}

	/* Otherwise the keys it stands for: a terminal's own, or the usual ones. */
	keys = &edit_keys[action];
	terminal = edit_terminal(window);
	key = keys->key;
	with = keys->modifiers;
	if (terminal) {
		key = keys->terminal_key;
		with = keys->terminal_modifiers;
	}
	if (key == 0U) {
		printf("ZWL EDIT action=%s via=none reason=no-keys surface=%u app=%s\n", edit_names[action], window->id, window->app_id);
		return ENOTSUP;
	}

	/* The modifiers for the key alone, the press and the release through the seat (the window's menu shortcuts too), and the modifiers as they were. */
	modifiers = server->modifiers;
	server->modifiers = with;
	zwl_seat_modifiers(server);
	time = (uint32_t)zwl_milliseconds();
	zwl_seat_key(server, time, key, 1U);
	zwl_seat_key(server, time, key, 0U);
	server->modifiers = modifiers;
	zwl_seat_modifiers(server);

	/* Succeeded: the keys were sent. */
	printf("ZWL EDIT action=%s via=keys key=%u modifiers=0x%x terminal=%d surface=%u app=%s\n", edit_names[action], key, with, terminal, window->id, window->app_id);
	return 0;
}

/*
 * Reads which operations the focused window can take now (bit 1 << action
 * set), for the tool face's buttons.  Returns 1 for a window with the
 * protocol (the bits follow its state), 0 for one without (the operations
 * that have keys, whatever its state), -1 without a focused window
 * (none).
 */
int
zwl_edit_state(
	struct zwl_server *server,
	uint32_t *enabled)
{
	struct zwl_object *window;
	struct zwl_object *edit;
	unsigned action;
	int terminal;

	/* The focused window and its edit object. */
	*enabled = 0U;
	edit = edit_focused(server, &window);
	if (window == NULL)
		return -1;

	/* A window with the protocol: its operations, as its state allows. */
	if (edit != NULL) {
		*enabled = edit_enabled(edit->edit_actions, edit->edit_flags);
		return 1;
	}

	/* Without it: each operation that has keys. */
	terminal = edit_terminal(window);
	for (action = 0; action < ZWL_EDIT_ACTIONS; action++) {
		if ((!terminal && edit_keys[action].key != 0U) || (terminal && edit_keys[action].terminal_key != 0U))
			*enabled |= 1U << action;
	}

	/* Succeeded: the operations with keys. */
	return 0;
}

/*
 * Brings forward the window focused before the one on top (the second
 * highest in the stacking order of the desktop shown), which then has the
 * focus; calling it again goes back.  Returns 0, or ENOENT when there is no
 * such window.
 */
int
zwl_focus_previous(
	struct zwl_server *server)
{
	struct zwl_client *client;
	struct zwl_object *surface;
	struct zwl_object *top;
	struct zwl_object *second;

	/* The two highest windows of the desktop shown, as zwl_top_window counts them. */
	top = NULL;
	second = NULL;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			/* A shown window, not the desktop's icons. */
			if (surface->kind != ZWL_SURFACE || surface->dead || !surface->mapped)
				continue;
			if (surface->desktop != server->desktop || surface->minimized)
				continue;
			if (zwl_desktop_is(surface))
				continue;

			/* Kept in order. */
			if (top == NULL || surface->map_order > top->map_order) {
				second = top;
				top = surface;
			} else if (second == NULL || surface->map_order > second->map_order) {
				second = surface;
			}
		}
	}

	/* None to go back to. */
	if (second == NULL) {
		printf("ZWL FOCUS previous none\n");
		return ENOENT;
	}

	/* It comes forward and has the focus (the keyboard, if open, stays). */
	zwl_glass_raise(server, second);
	printf("ZWL FOCUS previous surface=%u app=%s from=%u\n", second->id, second->app_id, top->id);

	/* Succeeded. */
	return 0;
}

/*
 * Takes the shortcuts that stand in for the tool face's buttons: Super+Alt
 * with C, X, V, Z, Y, A, S, E (the operations), P (the previous
 * application), Q (the state, logged), H (the clipboard's history,
 * logged) and 1 ... 0 (an item of it pasted).  Returns 1 when the key was
 * one.
 */
int
zwl_edit_key(
	struct zwl_server *server,
	uint32_t key,
	uint32_t state)
{
	uint32_t enabled;
	unsigned action;
	int protocol;

	/* Only with Super and Alt held. */
	if ((server->modifiers & (EDIT_SUPER | EDIT_ALT)) != (EDIT_SUPER | EDIT_ALT))
		return 0;

	/* P: the previous application, on the press. */
	if (key == EDIT_KEY_P) {
		if (state != 0U)
			(void)zwl_focus_previous(server);
		return 1;
	}

	/* H: the clipboard's history, logged on the press (clipboard.c). */
	if (key == EDIT_KEY_H) {
		if (state != 0U)
			zwl_clipboard_history_log(server);
		return 1;
	}

	/* 1 ... 9 and 0: the history's items 0 ... 9 pasted, on the press. */
	if (key >= EDIT_KEY_1 && key <= EDIT_KEY_0) {
		if (state != 0U)
			(void)zwl_clipboard_history_paste(server, key - EDIT_KEY_1);
		return 1;
	}

	/* Q: what the buttons would show, logged on the press. */
	if (key == EDIT_KEY_Q) {
		if (state != 0U) {
			protocol = zwl_edit_state(server, &enabled);
			printf("ZWL EDIT enabled=0x%x protocol=%d\n", enabled, protocol);
		}
		return 1;
	}

	/* An operation's key, on the press. */
	for (action = 0; action < ZWL_EDIT_ACTIONS; action++) {
		if (key != edit_shortcut_keys[action])
			continue;
		if (state != 0U)
			(void)zwl_edit_action(server, action);
		return 1;
	}

	/* Succeeded: not a shortcut of these. */
	return 0;
}

/* Makes a window's edit object (get_edit: the new ID and the window's xdg_toplevel). */
static int
edit_create(
	struct zwl_object *manager,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *toplevel;
	struct zwl_object *created;
	uint32_t id;

	/* The new ID and one of the client's toplevels. */
	if (size != 8U)
		return EPROTO;
	id = edit_word(bytes, 0U);
	toplevel = zwl_find(manager->client, edit_word(bytes, 4U));
	if (toplevel == NULL || toplevel->kind != ZWL_TOPLEVEL)
		return EPROTO;

	/* The edit object, naming its window, with no operations yet. */
	created = zwl_create(manager->client, id, ZWL_EDIT, manager->version);
	if (created == NULL)
		return EPROTO;
	created->top = toplevel;
	created->edit_actions = 0U;
	created->edit_flags = 0U;
	printf("ZWL EDIT create client=%llu edit=%u toplevel=%u\n", (unsigned long long)manager->client->number, id, toplevel->id);

	/* Succeeded: the window takes actions once it says which. */
	return 0;
}

/*
 * Finds the focused window (*window, NULL for none) and its edit object
 * (returned, NULL for none).
 */
static struct zwl_object *
edit_focused(
	struct zwl_server *server,
	struct zwl_object **window)
{
	struct zwl_object *object;
	struct zwl_object *focus;

	/* The window with the keyboard's focus. */
	focus = server->focus;
	*window = NULL;
	if (focus == NULL || focus->dead || focus->client->fatal)
		return NULL;
	*window = focus;

	/* Its client's edit object that names its toplevel. */
	for (object = focus->client->objects; object != NULL; object = object->next) {
		if (object->kind != ZWL_EDIT || object->dead || object->top == NULL)
			continue;
		if (object->top->surface == focus)
			return object;
	}

	/* Succeeded: it has none. */
	return NULL;
}

/* Tells whether a window is a terminal's (its application ID in edit_terminals). */
static int
edit_terminal(
	const struct zwl_object *window)
{
	size_t index;
	int same;

	/* Each terminal's application ID. */
	for (index = 0; index < sizeof(edit_terminals) / sizeof(edit_terminals[0]); index++) {
		same = strcmp(window->app_id, edit_terminals[index]);
		if (same == 0)
			return 1;
	}

	/* Succeeded: not a terminal. */
	return 0;
}

/*
 * Works out which of the operations a window carries out it can take in
 * its state: copy and cut with a selection, paste with something to
 * paste, undo and redo with something to undo or redo, select_begin when
 * no selection is being made and select_end when one is, select_all always.
 */
static uint32_t
edit_enabled(
	uint32_t actions,
	uint32_t flags)
{
	uint32_t enabled;

	/* Every operation it carries out, then those its state rules out. */
	enabled = actions;
	if ((flags & ZWL_EDIT_HAS_SELECTION) == 0U)
		enabled &= ~((1U << ZWL_EDIT_COPY) | (1U << ZWL_EDIT_CUT));
	if ((flags & ZWL_EDIT_CAN_PASTE) == 0U)
		enabled &= ~(1U << ZWL_EDIT_PASTE);
	if ((flags & ZWL_EDIT_CAN_UNDO) == 0U)
		enabled &= ~(1U << ZWL_EDIT_UNDO);
	if ((flags & ZWL_EDIT_CAN_REDO) == 0U)
		enabled &= ~(1U << ZWL_EDIT_REDO);
	if ((flags & ZWL_EDIT_SELECTING) != 0U)
		enabled &= ~(1U << ZWL_EDIT_SELECT_BEGIN);
	else
		enabled &= ~(1U << ZWL_EDIT_SELECT_END);

	/* Succeeded: what the buttons show as usable. */
	return enabled;
}

/* Reads one native-endian protocol word. */
static uint32_t
edit_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The caller has checked the payload's size. */
	memcpy(&word, bytes + offset, sizeof(word));
	return word;
}
