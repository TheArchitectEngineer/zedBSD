/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The input method's protocol side (ws095-p004, plan/ws095/design.md
 * sections 4 to 6): the input method object, its keyboard grab, the
 * virtual keyboard it gives unused keys back on, and zdesktop's status.
 *
 * Each key press the grab hears goes to the language chosen; what the
 * engine makes becomes set_preedit_string, commit_string and a commit,
 * which is sent for every press so that zdesktop hears an answer.  A key
 * the engine does not use goes back on the virtual keyboard, and so does
 * its release.  Alt+Space (zdesktop's next) commits what is composed and
 * chooses the next language; zdesktop hears the language and whether text
 * is being composed on the status.  A deactivation drops what is composed:
 * zdesktop has already committed the preedit to the application.  Nothing
 * typed is written to the log.
 */

#include "program.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The wl_keyboard key states. */
#define METHOD_KEY_RELEASED	0U
#define METHOD_KEY_PRESSED	1U

static void method_activate(void *data, struct zwp_input_method_v2 *method);
static void method_deactivate(void *data, struct zwp_input_method_v2 *method);
static void method_surrounding_text(void *data, struct zwp_input_method_v2 *method, const char *text, uint32_t cursor, uint32_t anchor);
static void method_text_change_cause(void *data, struct zwp_input_method_v2 *method, uint32_t cause);
static void method_content_type(void *data, struct zwp_input_method_v2 *method, uint32_t hint, uint32_t purpose);
static void method_done(void *data, struct zwp_input_method_v2 *method);
static void method_unavailable(void *data, struct zwp_input_method_v2 *method);
static void grab_keymap(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, uint32_t format, int32_t fd, uint32_t size);
static void grab_key(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void grab_modifiers(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
static void grab_repeat_info(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, int32_t rate, int32_t delay);
static void status_next(void *data, struct keiland_ime_status_v1 *status);
static void status_select(void *data, struct keiland_ime_status_v1 *status, const char *id);
static void method_send(struct program *program);
static void method_choose(struct program *program, unsigned index);

/*
 * The input method's events.
 */
static const struct zwp_input_method_v2_listener method_listener = {
	method_activate,
	method_deactivate,
	method_surrounding_text,
	method_text_change_cause,
	method_content_type,
	method_done,
	method_unavailable
};

/*
 * The keyboard grab's events.
 */
static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
	grab_keymap,
	grab_key,
	grab_modifiers,
	grab_repeat_info
};

/*
 * zdesktop's status events.
 */
static const struct keiland_ime_status_v1_listener status_listener = {
	status_next,
	status_select
};

/*
 * Makes the input method, its keyboard grab, the virtual keyboard and the
 * status, and tells zdesktop the language.
 *
 * Returns 0, or -1 when an object cannot be made.
 */
int
program_method_start(
	struct program *program)
{
	int status;

	/* The input method of the seat. */
	program->method = zwp_input_method_manager_v2_get_input_method(program->method_manager, program->seat);
	if (program->method == NULL)
		return -1;

	status = zwp_input_method_v2_add_listener(program->method, &method_listener, program);
	if (status != 0)
		return -1;

	/* The keyboard, whose keys zdesktop sends while a text input is served. */
	program->grab = zwp_input_method_v2_grab_keyboard(program->method);
	if (program->grab == NULL)
		return -1;

	status = zwp_input_method_keyboard_grab_v2_add_listener(program->grab, &grab_listener, program);
	if (status != 0)
		return -1;

	/* The virtual keyboard the unused keys go back on. */
	program->keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(program->keyboard_manager, program->seat);
	if (program->keyboard == NULL)
		return -1;

	/* zdesktop's status. */
	program->status = keiland_ime_status_manager_v1_get_status(program->status_manager);
	if (program->status == NULL)
		return -1;

	status = keiland_ime_status_v1_add_listener(program->status, &status_listener, program);
	if (status != 0)
		return -1;

	/* Succeeded: zdesktop hears the language chosen. */
	program_announce_language(program);
	return 0;
}

/*
 * Tells zdesktop the language chosen and its label.
 */
void
program_announce_language(
	struct program *program)
{
	const struct ime_engine_ops *ops;

	/* The language's ID and its short label. */
	ops = program->engines[program->current].ops;
	keiland_ime_status_v1_language(program->status, ops->id, ops->label);
	printf("KEI-IME LANGUAGE id=%s\n", ops->id);
}

/*
 * Notes an activation, applied at done.
 */
static void
method_activate(
	void *data,
	struct zwp_input_method_v2 *method)
{
	struct program *program;

	UNUSED_PARAMETER(method);

	/* A text input is served from the next done. */
	program = data;
	program->pending_active = 1;
	program->pending_active_set = 1;
}

/*
 * Notes a deactivation, applied at done.
 */
static void
method_deactivate(
	void *data,
	struct zwp_input_method_v2 *method)
{
	struct program *program;

	UNUSED_PARAMETER(method);

	/* No text input is served from the next done. */
	program = data;
	program->pending_active = 0;
	program->pending_active_set = 1;
}

/*
 * Takes the text around the cursor, which the engines do not use yet.
 */
static void
method_surrounding_text(
	void *data,
	struct zwp_input_method_v2 *method,
	const char *text,
	uint32_t cursor,
	uint32_t anchor)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(method);
	UNUSED_PARAMETER(text);
	UNUSED_PARAMETER(cursor);
	UNUSED_PARAMETER(anchor);
}

/*
 * Takes what changed the text last, which the engines do not use yet.
 */
static void
method_text_change_cause(
	void *data,
	struct zwp_input_method_v2 *method,
	uint32_t cause)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(method);
	UNUSED_PARAMETER(cause);
}

/*
 * Notes the field's content type, applied at done.
 */
static void
method_content_type(
	void *data,
	struct zwp_input_method_v2 *method,
	uint32_t hint,
	uint32_t purpose)
{
	struct program *program;

	UNUSED_PARAMETER(method);

	/* The hints and the purpose wait for done. */
	program = data;
	program->pending_hint = hint;
	program->pending_purpose = purpose;
}

/*
 * Applies the state zdesktop sent: an activation or a deactivation starts
 * the engines from nothing, and the content type tells them whether to
 * learn.
 */
static void
method_done(
	void *data,
	struct zwp_input_method_v2 *method)
{
	struct program *program;
	unsigned i;

	UNUSED_PARAMETER(method);

	/* The number of dones is the serial of the next commit. */
	program = data;
	program->done_count++;

	/* A change of the text input served drops what was being composed. */
	if (program->pending_active_set) {
		program->pending_active_set = 0;
		program->active = program->pending_active;
		for (i = 0; i < program->engine_count; i++)
			program->engines[i].ops->reset(&program->engines[i], false, program->out);
		program->composing = 0;
		keiland_ime_status_v1_composing(program->status, 0);
		printf("KEI-IME %s\n", program->active ? "ACTIVATE" : "DEACTIVATE");
	}

	/* A secret field teaches nothing (the engines' content_type). */
	for (i = 0; i < program->engine_count; i++)
		program->engines[i].ops->content_type(&program->engines[i], program->pending_hint, program->pending_purpose);
}

/*
 * Ends the program: another input method holds the seat.
 */
static void
method_unavailable(
	void *data,
	struct zwp_input_method_v2 *method)
{
	struct program *program;

	UNUSED_PARAMETER(method);

	/* main's loop ends on this. */
	program = data;
	program->unavailable = 1;
	printf("KEI-IME UNAVAILABLE\n");
}

/*
 * Gives the virtual keyboard the grab's keymap, as the protocol asks before
 * any key.
 */
static void
grab_keymap(
	void *data,
	struct zwp_input_method_keyboard_grab_v2 *grab,
	uint32_t format,
	int32_t fd,
	uint32_t size)
{
	struct program *program;

	UNUSED_PARAMETER(grab);

	/* The first keymap goes to the virtual keyboard; the descriptor is ours to close. */
	program = data;
	if (!program->keymap_sent && program->keyboard != NULL) {
		zwp_virtual_keyboard_v1_keymap(program->keyboard, format, fd, size);
		program->keymap_sent = 1;
	}

	close(fd);
}

/*
 * Gives one key to the language chosen, sends what it made, and gives the
 * key back when the language does not use it.
 */
static void
grab_key(
	void *data,
	struct zwp_input_method_keyboard_grab_v2 *grab,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	struct program *program;
	struct ime_engine *engine;
	struct ime_key typed;

	UNUSED_PARAMETER(grab);
	UNUSED_PARAMETER(serial);

	program = data;

	/* A release goes back where its press went. */
	if (state == METHOD_KEY_RELEASED) {
		if (key < PROGRAM_KEYS && program->passed[key]) {
			program->passed[key] = 0;
			zwp_virtual_keyboard_v1_key(program->keyboard, time, key, METHOD_KEY_RELEASED);
		}

		return;
	}

	/* The key as the engines see it. */
	typed.code = key;
	typed.character = program_key_character(key, program->modifiers);
	typed.modifiers = program_key_modifiers(program->modifiers);

	/* The language chosen acts on it; a key heard while no text input is served goes back. */
	engine = &program->engines[program->current];
	if (program->active) {
		engine->ops->key(engine, &typed, program->out);
	} else {
		ime_output_clear(program->out);
		program->out->pass_key = true;
	}

	/* What it made (always sent, so that zdesktop hears an answer). */
	method_send(program);

	/* A key the language does not use goes back to the application. */
	if (program->out->pass_key) {
		zwp_virtual_keyboard_v1_key(program->keyboard, time, key, METHOD_KEY_PRESSED);
		if (key < PROGRAM_KEYS)
			program->passed[key] = 1;
	}
}

/*
 * Keeps the modifiers held.
 */
static void
grab_modifiers(
	void *data,
	struct zwp_input_method_keyboard_grab_v2 *grab,
	uint32_t serial,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group)
{
	struct program *program;

	UNUSED_PARAMETER(grab);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(latched);
	UNUSED_PARAMETER(locked);
	UNUSED_PARAMETER(group);

	/* Shift chooses the characters; the others tell the engines a shortcut. */
	program = data;
	program->modifiers = depressed;
}

/*
 * Takes the repeat zdesktop tells, which the program does not use yet.
 */
static void
grab_repeat_info(
	void *data,
	struct zwp_input_method_keyboard_grab_v2 *grab,
	int32_t rate,
	int32_t delay)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(grab);
	UNUSED_PARAMETER(rate);
	UNUSED_PARAMETER(delay);
}

/*
 * Chooses the next language (Alt+Space).
 */
static void
status_next(
	void *data,
	struct keiland_ime_status_v1 *status)
{
	struct program *program;

	UNUSED_PARAMETER(status);

	/* The one after the language chosen, round the list. */
	program = data;
	method_choose(program, (program->current + 1U) % program->engine_count);
}

/*
 * Chooses a language by its ID (the Japanese keyboard's keys).
 */
static void
status_select(
	void *data,
	struct keiland_ime_status_v1 *status,
	const char *id)
{
	struct program *program;
	unsigned i;
	int order;

	UNUSED_PARAMETER(status);

	/* Looks for the language. */
	program = data;
	for (i = 0; i < program->engine_count; i++) {
		order = strcmp(program->engines[i].ops->id, id);
		if (order == 0) {
			method_choose(program, i);
			return;
		}
	}

	/* An unknown language is ignored. */
	printf("KEI-IME SELECT unknown\n");
}

/*
 * Sends what the engine made: the text to commit, the preedit and its
 * cursor, and the commit that applies them; zdesktop hears whether text is
 * being composed.
 */
static void
method_send(
	struct program *program)
{
	struct ime_output *out;
	unsigned composing;

	out = program->out;

	/* The text to commit, when there is some. */
	if (out->commit_length != 0U)
		zwp_input_method_v2_commit_string(program->method, out->commit);

	/* The preedit (empty for none) and the commit, naming the dones heard. */
	zwp_input_method_v2_set_preedit_string(program->method, out->preedit, out->cursor_begin, out->cursor_end);
	zwp_input_method_v2_commit(program->method, program->done_count);

	/* Whether text is being composed decides where zdesktop puts the menu keys. */
	composing = 0;
	if (out->composing)
		composing = 1;
	if (composing != program->composing) {
		program->composing = composing;
		keiland_ime_status_v1_composing(program->status, composing);
	}
}

/*
 * Chooses a language: what the old one composes is committed first.
 */
static void
method_choose(
	struct program *program,
	unsigned index)
{
	struct ime_engine *engine;

	/* The same language needs nothing. */
	if (index == program->current)
		return;

	/* The old language commits what it composes. */
	engine = &program->engines[program->current];
	engine->ops->reset(engine, true, program->out);
	if (program->active)
		method_send(program);

	/* The new language, told to zdesktop. */
	program->current = index;
	program_announce_language(program);
}
