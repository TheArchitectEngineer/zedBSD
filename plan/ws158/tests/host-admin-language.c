/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws158-p004: the host test of account-admin's system-language request
 * (userland/base/account-admin/edit.c compiled unchanged): the operation's
 * word, its one argument held as the name, the languages known ("en",
 * "ja") and the refusals (another language, no argument, two arguments,
 * no password), and the other operations still read as before.
 *
 *   plan/ws158/tests/run-host-admin-language.sh
 */

#include "userland/base/account-admin/edit.h"

#include <stdio.h>
#include <string.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);
static int parse(const char *text, struct admin_request *request);

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Reads a request from a copy of a text (the parser cuts its lines in place). */
static int
parse(
	const char *text,
	struct admin_request *request)
{
	static char copy[ADMIN_REQUEST_MAX + 1U];
	size_t length;
	int reason;

	/* The copy, and the request read from it. */
	length = strlen(text);
	memcpy(copy, text, length + 1U);
	reason = admin_parse(copy, length, request);

	/* Succeeded: the parser's answer. */
	return reason;
}

/* Runs every case. */
int
main(void)
{
	struct admin_request request;
	int reason;

	/* 1. Japanese, then English: read, the language as the name. */
	reason = parse("secret\nsystem-language\nja\n", &request);
	check(reason == ADMIN_OK && request.operation == ADMIN_SYSTEM_LANGUAGE, "ja: read");
	check(reason == ADMIN_OK && strcmp(request.name, "ja") == 0, "ja: the language");
	reason = parse("secret\nsystem-language\nen", &request);
	check(reason == ADMIN_OK && strcmp(request.name, "en") == 0, "en without a last line end");
	check(strcmp(admin_operation_word(ADMIN_SYSTEM_LANGUAGE), "system-language") == 0, "the operation's word");

	/* 2. Refused: a language not known, an empty one, none, two, no password. */
	reason = parse("secret\nsystem-language\nfr\n", &request);
	check(reason == ADMIN_BAD_REQUEST, "fr refused");
	reason = parse("secret\nsystem-language\n\n", &request);
	check(reason == ADMIN_BAD_REQUEST, "an empty language refused");
	reason = parse("secret\nsystem-language\n", &request);
	check(reason == ADMIN_BAD_REQUEST, "no language refused");
	reason = parse("secret\nsystem-language\nja\nen\n", &request);
	check(reason == ADMIN_BAD_REQUEST, "two languages refused");
	reason = parse("\nsystem-language\nja\n", &request);
	check(reason == ADMIN_BAD_REQUEST, "no password refused");
	reason = parse("secret\nsystem-language\nja/../x\n", &request);
	check(reason == ADMIN_BAD_REQUEST, "a path refused");

	/* 3. The other operations are read as before. */
	reason = parse("secret\ngroup-add\nkei\nwheel\n", &request);
	check(reason == ADMIN_OK && request.operation == ADMIN_GROUP_ADD, "group-add still read");
	reason = parse("secret\nremove\nkei\nkeep-home\n", &request);
	check(reason == ADMIN_OK && request.operation == ADMIN_REMOVE, "remove still read");
	check(admin_language_valid("ja") && admin_language_valid("en") && !admin_language_valid("EN"), "the languages known");

	/* The result. */
	if (failures != 0) {
		printf("host-admin-language: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-admin-language: ok (%d checks)\n", checks);
	return 0;
}
