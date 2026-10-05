/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws154-p003: the host test of the SKK engine (userland/desktop/ime/
 * skk-engine.c), driven through engine.h as the input method drives it.
 *
 *     host-skk DICTIONARY WORKDIR [SYSTEM-DICTIONARY...]
 *
 * Each case types a script of keys into a fresh engine (with its own user
 * dictionary in WORKDIR) and compares what the application would hold
 * (the text committed, and the keys given back, typed) and the preedit
 * left with what it expects.  A script is characters, and <SPC>, <RET>,
 * <BS>, <C-g> and <C-j>.  With the image's dictionaries (REmacs's
 * SKK-JISYO.X and SKK-JISYO.remacs) named after WORKDIR, two words are
 * converted with them too.  Prints one line a case and host-skk: PASS or
 * host-skk: FAIL.
 */

#include "skk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The code a letter key is given (the engine reads the character). */
#define TEST_KEY_LETTER		30U

/* What the application holds after a script, at most. */
#define TEST_TEXT_MAX		1024U

/*
 * One case: its name, the field's hint and purpose, the script, what the
 * application must hold after it and the preedit that must be left.
 */
struct skk_case {
	const char *name;
	uint32_t hint;
	uint32_t purpose;
	const char *script;
	const char *expected;
	const char *preedit;
};

int main(int argc, char **argv);
static int run_case(const struct skk_case *item, const char *dictionary, const char *user);
static int run_script(struct ime_engine *engine, const char *script, char *text, size_t size, char *preedit, size_t preedit_size);
static size_t script_key(const char *script, struct ime_key *key);
static void take_output(const struct ime_output *out, const struct ime_key *key, char *text, size_t size);
static int check_listing(const char *dictionary, const char *user);
static int check_saved(const char *dictionary, const char *user);
static int check_system(const char *first, const char *second, const char *user);
static int check_modes(const char *dictionary, const char *user);

/* The cases. */
static const struct skk_case cases[] = {
	{ "kana straight in", 0, 0, "nihon<RET>", "\xe3\x81\xab\xe3\x81\xbb\xe3\x82\x93\n", "" },
	{ "a reading converted", 0, 0, "Nihon<SPC><RET>", "\xe6\x97\xa5\xe6\x9c\xac", "" },
	{ "the second candidate", 0, 0, "Kanji<SPC><SPC><RET>", "\xe5\xb9\xb9\xe4\xba\x8b", "" },
	{ "x for the previous", 0, 0, "Kanji<SPC><SPC>x<RET>", "\xe6\xbc\xa2\xe5\xad\x97", "" },
	{ "okurigana converts at once", 0, 0, "KaKu", "", "\xe2\x96\xbc\xe6\x9b\xb8\xe3\x81\x8f" },
	{ "okurigana put in", 0, 0, "KaKu<RET>", "\xe6\x9b\xb8\xe3\x81\x8f", "" },
	{ "okurigana's next candidate", 0, 0, "OkuRu<SPC><RET>", "\xe8\xb4\x88\xe3\x82\x8b", "" },
	{ "okurigana with sokuon", 0, 0, "TaTte<RET>", "\xe7\xab\x8b\xe3\x81\xa3\xe3\x81\xa6", "" },
	{ "typing on puts the candidate in", 0, 0, "Nihon<SPC>a", "\xe6\x97\xa5\xe6\x9c\xac\xe3\x81\x82", "" },
	{ "a listed candidate chosen", 0, 0, "Kanji<SPC><SPC><SPC><SPC><SPC>d", "\xe9\x96\x91\xe4\xba\x8b", "" },
	{ "q toggles katakana", 0, 0, "qaiqa", "\xe3\x82\xa2\xe3\x82\xa4\xe3\x81\x82", "" },
	{ "q on a reading", 0, 0, "Aiq", "\xe3\x82\xa2\xe3\x82\xa4", "" },
	{ "Latin mode and back", 0, 0, "labc<C-j>a", "abc\xe3\x81\x82", "" },
	{ "wide Latin and back", 0, 0, "Lab<C-j>i", "\xef\xbd\x81\xef\xbd\x82\xe3\x81\x84", "" },
	{ "C-g drops a reading", 0, 0, "Kan<C-g>a", "\xe3\x81\x82", "" },
	{ "C-g goes back to the reading", 0, 0, "Nihon<SPC><C-g>", "", "\xe2\x96\xbd\xe3\x81\xab\xe3\x81\xbb\xe3\x82\x93" },
	{ "Backspace on a reading", 0, 0, "Aii<BS><SPC><RET>", "\xe6\x84\x9b", "" },
	{ "an annotation is left out", 0, 0, "Ai<SPC><SPC><RET>", "\xe8\x97\x8d", "" },
	{ "Enter puts a reading in", 0, 0, "Kanji<RET>", "\xe3\x81\x8b\xe3\x82\x93\xe3\x81\x98", "" },
	{ "a registration", 0, 0, "Mimi<SPC>", "", "[\xe7\x99\xbb\xe9\x8c\xb2]\xe3\x81\xbf\xe3\x81\xbf " },
	{ "a word registered and learned", 0, 0, "Mimi<SPC>Nihon<SPC><RET><RET>Mimi<SPC><RET>",
	  "\xe6\x97\xa5\xe6\x9c\xac\xe6\x97\xa5\xe6\x9c\xac", "" },
	{ "a registration given up", 0, 0, "Mimi<SPC><C-g>", "", "\xe2\x96\xbd\xe3\x81\xbf\xe3\x81\xbf" },
	{ "a choice learned", 0, 0, "Kanji<SPC><SPC><RET>Kanji<SPC><RET>",
	  "\xe5\xb9\xb9\xe4\xba\x8b\xe5\xb9\xb9\xe4\xba\x8b", "" },
	{ "a password field learns nothing", 0, IME_PURPOSE_PASSWORD, "Kanji<SPC><SPC><RET>Kanji<SPC><RET>",
	  "\xe5\xb9\xb9\xe4\xba\x8b\xe6\xbc\xa2\xe5\xad\x97", "" },
};

/*
 * Runs every case, then the listing and the saving checks.
 */
int
main(
	int argc,
	char **argv)
{
	char user[1024];
	size_t i;
	int failed;
	int passed;

	/* The dictionary and the working directory. */
	if (argc < 3) {
		fprintf(stderr, "usage: host-skk DICTIONARY WORKDIR\n");
		return 2;
	}

	/* Each case with a user dictionary of its own. */
	failed = 0;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		/* A fresh user dictionary. */
		snprintf(user, sizeof(user), "%s/user-%zu", argv[2], i);
		(void)remove(user);
		passed = run_case(&cases[i], argv[1], user);
		if (!passed)
			failed++;
	}

	/* The candidate window's page. */
	snprintf(user, sizeof(user), "%s/user-listing", argv[2]);
	(void)remove(user);
	passed = check_listing(argv[1], user);
	if (!passed)
		failed++;

	/* A choice saved and read back by a new engine. */
	snprintf(user, sizeof(user), "%s/user-saved", argv[2]);
	(void)remove(user);
	passed = check_saved(argv[1], user);
	if (!passed)
		failed++;

	/* The modes as languages of their own. */
	snprintf(user, sizeof(user), "%s/user-modes", argv[2]);
	(void)remove(user);
	passed = check_modes(argv[1], user);
	if (!passed)
		failed++;

	/* The image's dictionaries, when named. */
	if (argc >= 5) {
		snprintf(user, sizeof(user), "%s/user-system", argv[2]);
		(void)remove(user);
		passed = check_system(argv[3], argv[4], user);
		if (!passed)
			failed++;
	}

	/* The verdict. */
	if (failed != 0) {
		printf("host-skk: FAIL (%d)\n", failed);
		return 1;
	}

	/* Succeeded: every case. */
	printf("host-skk: PASS\n");
	return 0;
}

/*
 * Runs one case in a fresh engine.  Returns 1 when it gives what it
 * expects.
 */
static int
run_case(
	const struct skk_case *item,
	const char *dictionary,
	const char *user)
{
	struct skk_config config;
	struct ime_engine engine;
	char text[TEST_TEXT_MAX];
	char preedit[IME_TEXT_MAX];
	int error;
	int same_text;
	int same_preedit;

	/* The engine with the test dictionary. */
	memset(&config, 0, sizeof(config));
	config.dictionaries[0] = dictionary;
	config.user_dictionary = user;
	error = skk_engine_create(&engine, &config);
	if (error != 0) {
		printf("%s: FAIL (create %d)\n", item->name, error);
		return 0;
	}

	/* The field, then the script. */
	engine.ops->content_type(&engine, item->hint, item->purpose);
	(void)run_script(&engine, item->script, text, sizeof(text), preedit, sizeof(preedit));
	engine.ops->destroy(&engine);

	/* What the application holds and the preedit. */
	same_text = strcmp(text, item->expected);
	same_preedit = strcmp(preedit, item->preedit);
	if (same_text != 0 || same_preedit != 0) {
		printf("%s: FAIL (text \"%s\" preedit \"%s\")\n", item->name, text, preedit);
		return 0;
	}

	/* Succeeded: as expected. */
	printf("%s: ok\n", item->name);
	return 1;
}

/*
 * Types a script; text gets what the application would hold and preedit
 * the preedit left.  Returns the candidates shown after the last key.
 */
static int
run_script(
	struct ime_engine *engine,
	const char *script,
	char *text,
	size_t size,
	char *preedit,
	size_t preedit_size)
{
	struct ime_output *out;
	struct ime_key key;
	size_t used;
	int shown;

	/* The output, large, on the heap. */
	text[0] = '\0';
	preedit[0] = '\0';
	out = calloc(1, sizeof(*out));
	if (out == NULL)
		return 0;

	/* Each key of the script. */
	shown = 0;
	while (*script != '\0') {
		/* The key, and what it made. */
		used = script_key(script, &key);
		script += used;
		engine->ops->key(engine, &key, out);
		take_output(out, &key, text, size);
		snprintf(preedit, preedit_size, "%s", out->preedit);
		shown = (int)out->candidate_count;
		if (!out->candidates_shown)
			shown = 0;
	}

	/* The candidates shown at the end. */
	free(out);
	return shown;
}

/*
 * Reads the next key of a script.  Returns how many of its characters it
 * took.
 */
static size_t
script_key(
	const char *script,
	struct ime_key *key)
{
	int named;

	/* A plain character. */
	memset(key, 0, sizeof(*key));
	key->code = TEST_KEY_LETTER;
	key->character = (unsigned char)script[0];
	if (script[0] != '<')
		return 1;

	/* Space. */
	named = strncmp(script, "<SPC>", 5);
	if (named == 0) {
		key->code = IME_KEY_SPACE;
		key->character = ' ';
		return 5;
	}

	/* Enter. */
	named = strncmp(script, "<RET>", 5);
	if (named == 0) {
		key->code = IME_KEY_ENTER;
		key->character = '\r';
		return 5;
	}

	/* Backspace. */
	named = strncmp(script, "<BS>", 4);
	if (named == 0) {
		key->code = IME_KEY_BACKSPACE;
		key->character = 0;
		return 4;
	}

	/* C-g. */
	named = strncmp(script, "<C-g>", 5);
	if (named == 0) {
		key->character = 'g';
		key->modifiers = IME_MOD_CTRL;
		return 5;
	}

	/* C-j. */
	named = strncmp(script, "<C-j>", 5);
	if (named == 0) {
		key->character = 'j';
		key->modifiers = IME_MOD_CTRL;
		return 5;
	}

	/* A lone < is the character. */
	return 1;
}

/*
 * Adds what one key made to what the application holds: the commit, then
 * the key itself when it was given back (Enter as a new line).
 */
static void
take_output(
	const struct ime_output *out,
	const struct ime_key *key,
	char *text,
	size_t size)
{
	size_t length;

	/* The text committed. */
	length = strlen(text);
	if (length + out->commit_length < size) {
		memcpy(text + length, out->commit, out->commit_length);
		length += out->commit_length;
		text[length] = '\0';
	}

	/* A key given back: Enter is a new line, a character is typed. */
	if (!out->pass_key || length + 1U >= size)
		return;

	/* Enter. */
	if (key->code == IME_KEY_ENTER) {
		text[length] = '\n';
		text[length + 1U] = '\0';
		return;
	}

	/* A printable character. */
	if (key->character >= 0x20U && key->character < 0x7fU) {
		text[length] = (char)key->character;
		text[length + 1U] = '\0';
	}
}

/*
 * Checks the candidate window: after the four shown one by one the fifth candidate and
 * the four after it are listed with their keys.
 */
static int
check_listing(
	const char *dictionary,
	const char *user)
{
	struct skk_config config;
	struct ime_engine engine;
	char text[TEST_TEXT_MAX];
	char preedit[IME_TEXT_MAX];
	int shown;
	int error;

	/* The engine. */
	memset(&config, 0, sizeof(config));
	config.dictionaries[0] = dictionary;
	config.user_dictionary = user;
	error = skk_engine_create(&engine, &config);
	if (error != 0) {
		printf("listing: FAIL (create %d)\n", error);
		return 0;
	}

	/* The conversion and four Spaces past the first four: the list of the five left. */
	shown = run_script(&engine, "Kanji<SPC><SPC><SPC><SPC><SPC>", text, sizeof(text), preedit, sizeof(preedit));
	engine.ops->destroy(&engine);
	if (shown != 5) {
		printf("listing: FAIL (shown %d)\n", shown);
		return 0;
	}

	/* Succeeded: listed. */
	printf("listing: ok\n");
	return 1;
}

/*
 * Checks that a choice is saved to the user's dictionary and that a new
 * engine reads it back.
 */
static int
check_saved(
	const char *dictionary,
	const char *user)
{
	struct skk_config config;
	struct ime_engine engine;
	char text[TEST_TEXT_MAX];
	char preedit[IME_TEXT_MAX];
	int error;
	int same;

	/* The first engine learns 幹事 and goes (saving it). */
	memset(&config, 0, sizeof(config));
	config.dictionaries[0] = dictionary;
	config.user_dictionary = user;
	error = skk_engine_create(&engine, &config);
	if (error != 0) {
		printf("saved: FAIL (create %d)\n", error);
		return 0;
	}

	/* The second candidate chosen, saved, and the engine goes. */
	(void)run_script(&engine, "Kanji<SPC><SPC><RET>", text, sizeof(text), preedit, sizeof(preedit));
	engine.ops->save(&engine);
	engine.ops->destroy(&engine);

	/* A second engine offers it first. */
	error = skk_engine_create(&engine, &config);
	if (error != 0) {
		printf("saved: FAIL (create again %d)\n", error);
		return 0;
	}

	/* The first candidate now. */
	(void)run_script(&engine, "Kanji<SPC><RET>", text, sizeof(text), preedit, sizeof(preedit));
	engine.ops->destroy(&engine);
	same = strcmp(text, "\xe5\xb9\xb9\xe4\xba\x8b");
	if (same != 0) {
		printf("saved: FAIL (text \"%s\")\n", text);
		return 0;
	}

	/* Succeeded: read back. */
	printf("saved: ok\n");
	return 1;
}

/*
 * Converts two words with the image's dictionaries: 書く (okurigana) and
 * 日本語.
 */
static int
check_system(
	const char *first,
	const char *second,
	const char *user)
{
	struct skk_config config;
	struct ime_engine engine;
	char text[TEST_TEXT_MAX];
	char preedit[IME_TEXT_MAX];
	int error;
	int same;

	/* The engine with both dictionaries. */
	memset(&config, 0, sizeof(config));
	config.dictionaries[0] = first;
	config.dictionaries[1] = second;
	config.user_dictionary = user;
	error = skk_engine_create(&engine, &config);
	if (error != 0) {
		printf("system dictionaries: FAIL (create %d)\n", error);
		return 0;
	}

	/* The two words. */
	(void)run_script(&engine, "KaKu<RET>Nihongo<SPC><RET>", text, sizeof(text), preedit, sizeof(preedit));
	engine.ops->destroy(&engine);
	same = strcmp(text, "\xe6\x9b\xb8\xe3\x81\x8f\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
	if (same != 0) {
		printf("system dictionaries: FAIL (text \"%s\")\n", text);
		return 0;
	}

	/* Succeeded: converted. */
	printf("system dictionaries: ok\n");
	return 1;
}

/*
 * Checks the modes as the desktop sees them: the ID after q, a mode taken
 * by its ID (the desktop's memory of an application), and an ID that is
 * not a mode refused.
 */
static int
check_modes(
	const char *dictionary,
	const char *user)
{
	struct skk_config config;
	struct ime_engine engine;
	char text[TEST_TEXT_MAX];
	char preedit[IME_TEXT_MAX];
	const char *id;
	const char *label;
	bool taken;
	int katakana;
	int latin;
	int passed;
	int error;

	/* The engine. */
	memset(&config, 0, sizeof(config));
	config.dictionaries[0] = dictionary;
	config.user_dictionary = user;
	error = skk_engine_create(&engine, &config);
	if (error != 0) {
		printf("modes: FAIL (create %d)\n", error);
		return 0;
	}

	/* q: katakana. */
	(void)run_script(&engine, "q", text, sizeof(text), preedit, sizeof(preedit));
	id = engine.ops->mode(&engine, &label);
	katakana = strcmp(id, "skk-katakana");

	/* Latin taken by its ID; a key then goes to the application. */
	taken = engine.ops->select(&engine, "skk-latin");
	id = engine.ops->mode(&engine, &label);
	latin = strcmp(id, "skk-latin");
	(void)run_script(&engine, "a", text, sizeof(text), preedit, sizeof(preedit));
	passed = strcmp(text, "a");

	/* Each as expected. */
	if (katakana != 0 || latin != 0 || !taken || passed != 0) {
		printf("modes: FAIL (katakana %d latin %d taken %d text \"%s\")\n", katakana, latin, taken, text);
		engine.ops->destroy(&engine);
		return 0;
	}

	/* Refuses another engine's ID. */
	taken = engine.ops->select(&engine, "ja");
	engine.ops->destroy(&engine);
	if (taken) {
		printf("modes: FAIL (ja taken)\n");
		return 0;
	}

	/* Succeeded: the modes are languages. */
	printf("modes: ok\n");
	return 1;
}
