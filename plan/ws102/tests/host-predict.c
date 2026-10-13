/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws102-p026: the host's test of the on-screen keyboard's words from the
 * Japanese engine (userland/desktop/ime/ja-engine.c, engine_predict) with
 * WS095's small dictionary: a reading with okurigana is converted whole
 * (よむ gives 読む, かく gives 書く, which the predictions alone never
 * gave), a reading of a word gives the word first (かいしゃ, 会社), a
 * sentence is converted (きょうはてんき), and a word offered by both the
 * conversion and the predictions is offered once.
 */

#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);
static int has_word(const char *list, const char *word);
static int first_word(const char *list, const char *word);
static int count_word(const char *list, const char *word);

/*
 * Runs the checks.  Returns 0 when all passed, 1 when one failed or the
 * engine could not be made.
 */
int
main(
	int argc,
	char **argv)
{
	struct ime_engine engine;
	struct ja_config config;
	char *list;
	size_t length;
	int error;
	int found;

	/* The dictionary named on the command line. */
	if (argc < 2) {
		printf("usage: host-predict DICTIONARY\n");
		return 1;
	}

	/* The answer's room. */
	list = malloc(4000U);
	if (list == NULL) {
		printf("host-predict: no memory\n");
		return 1;
	}

	/* The engine with the dictionary, no user dictionary to read. */
	config.system_dictionary = argv[1];
	config.supplement_dictionary = NULL;
	config.user_dictionary = "/nonexistent/host-predict-user.dict";
	error = ja_engine_create(&engine, &config);
	if (error != 0) {
		printf("host-predict: the engine was not made (%d)\n", error);
		free(list);
		return 1;
	}

	/* A verb with okurigana: 読む, 書く. */
	length = engine.ops->predict(&engine, "よむ", list, 4000U);
	printf("よむ: %s", list);
	found = has_word(list, "読む");
	check(length > 0U && found, "よむ: 読む (okurigana)");
	length = engine.ops->predict(&engine, "かく", list, 4000U);
	printf("かく: %s", list);
	found = first_word(list, "書く");
	check(length > 0U && found, "かく: 書く first");

	/* A noun: the word first, once. */
	length = engine.ops->predict(&engine, "かいしゃ", list, 4000U);
	printf("かいしゃ: %s", list);
	found = first_word(list, "会社");
	check(length > 0U && found, "かいしゃ: 会社 first");
	found = count_word(list, "会社");
	check(found == 1, "かいしゃ: 会社 once");

	/* A sentence converted whole. */
	length = engine.ops->predict(&engine, "きょうはてんき", list, 4000U);
	printf("きょうはてんき: %s", list);
	found = has_word(list, "今日は天気");
	check(length > 0U && found, "きょうはてんき: 今日は天気");

	/* The reading as typed is not offered. */
	found = has_word(list, "きょうはてんき");
	check(!found, "きょうはてんき: not the reading itself");

	/* Not kana: nothing converted, no crash. */
	length = engine.ops->predict(&engine, "abc", list, 4000U);
	check(length == 0U, "abc: no words");

	/* The engine and the room go. */
	engine.ops->destroy(&engine);
	free(list);

	/* The outcome. */
	if (failures != 0) {
		printf("host-predict: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-predict: PASS\n");
	return 0;
}

/* Prints a check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *text)
{
	/* A failure is counted. */
	if (!condition) {
		printf("FAIL: %s\n", text);
		failures++;
		return;
	}

	/* A pass. */
	printf("ok: %s\n", text);
}

/* Tells whether a list of "WORD\tREADING" lines has a word.  Returns 1 or 0. */
static int
has_word(
	const char *list,
	const char *word)
{
	int count;

	/* At least once. */
	count = count_word(list, word);
	if (count > 0)
		return 1;

	/* Not there. */
	return 0;
}

/* Tells whether a list's first line is a word.  Returns 1 or 0. */
static int
first_word(
	const char *list,
	const char *word)
{
	size_t length;
	int order;

	/* The word, then its tab, at the start. */
	length = strlen(word);
	order = strncmp(list, word, length);
	if (order != 0 || list[length] != '\t')
		return 0;

	/* Succeeded: the first line. */
	return 1;
}

/* Counts the lines of a list that are a word. */
static int
count_word(
	const char *list,
	const char *word)
{
	const char *line;
	int count;
	int first;

	/* Each line. */
	count = 0;
	for (line = list; *line != '\0'; line++) {
		first = first_word(line, word);
		if (first)
			count++;
		line = strchr(line, '\n');
		if (line == NULL)
			break;
	}

	/* The lines. */
	return count;
}
