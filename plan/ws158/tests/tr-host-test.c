/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libkeiland's translations (ws158-p002, translate.c):
 * catalogs written to a directory are read and their texts given; English
 * and a missing catalog give the source's text; the escapes, a context, a
 * plural's forms, the shared domain after the program's, a malformed line
 * and kl_tr_format's places are checked.  The last line is
 * "tr-host: PASS" or "tr-host: FAIL".
 *
 *   tr-host-test DIRECTORY
 */

#include <keiland.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* How many checks failed. */
static int test_failures;

static void test_same(const char *what, const char *got, const char *expected);
static void test_number(const char *what, int got, int expected);

/*
 * Runs the checks against the catalogs under a directory.
 */
int
main(
	int argc,
	char **argv)
{
	char out[64];
	char small[8];
	const char *english;
	int error;

	/* The directory the shell part wrote the catalogs into. */
	if (argc != 2) {
		fprintf(stderr, "usage: tr-host-test DIRECTORY\n");
		return 2;
	}

	/* English before any catalog, and English chosen. */
	test_same("before any", kl_tr_language(), "en");
	english = "Open";
	test_same("no catalog", kl_tr(english), "Open");
	error = kl_tr_open_directory(argv[1], "files", "en");
	test_number("open en", error, 0);
	test_same("en is the source's", kl_tr("Move to Trash"), "Move to Trash");

	/* Japanese: the program's catalog, then the shared one. */
	error = kl_tr_open_directory(argv[1], "files", "ja");
	test_number("open ja", error, 0);
	test_same("language", kl_tr_language(), "ja");
	test_same("msg", kl_tr("Move to Trash"), "ゴミ箱に移動");
	test_same("shared domain", kl_tr("Copy"), "コピー");
	test_same("own domain first", kl_tr("Cut"), "切り取り（Files）");
	test_same("not translated", kl_tr("Unknown text"), "Unknown text");
	test_same("escapes", kl_tr("Tab\there"), "タブ\tと\\と\n改行");
	test_same("context verb", kl_trc("verb", "Open"), "開く");
	test_same("context adjective", kl_trc("adjective", "Open"), "開いている");
	test_same("plain Open", kl_tr("Open"), "開く（既定）");
	test_same("context not there", kl_trc("noun", "Open"), "Open");
	test_same("plural one", kl_trn("{1} item", "{1} items", 1UL), "{1} 個");
	test_same("plural many", kl_trn("{1} item", "{1} items", 5UL), "{1} 個");
	test_same("plural not there", kl_trn("{1} file", "{1} files", 2UL), "{1} files");
	test_same("plural not there, one", kl_trn("{1} file", "{1} files", 1UL), "{1} file");
	test_same("malformed line left out", kl_tr("Broken"), "Broken");
	test_same("later line wins", kl_tr("Twice"), "二度目");

	/* Places in any order, a brace, and a text cut to fit. */
	error = kl_tr_format(out, sizeof(out), "{2} の {1} を {{開く}", "a.txt", "Documents", (const char *)NULL);
	test_number("format", error, 0);
	test_same("format text", out, "Documents の a.txt を {開く}");
	error = kl_tr_format(out, sizeof(out), "{1} と {3}", "x", "y", (const char *)NULL);
	test_number("format missing place", error, EINVAL);
	error = kl_tr_format(small, sizeof(small), "{1}{1}", "abcde", (const char *)NULL);
	test_number("format cut", error, ERANGE);
	test_same("format cut text", small, "abcdeab");

	/* A bad name is refused, and the texts are English after it. */
	error = kl_tr_open_directory(argv[1], "../etc", "ja");
	test_number("bad domain", error, EINVAL);
	test_same("English after a failure", kl_tr("Move to Trash"), "Move to Trash");
	test_same("language code 1", kl_tr_language_code(1), "ja");
	test_number("language code 2", kl_tr_language_code(2) == NULL, 1);

	/* A language without catalogs is English. */
	error = kl_tr_open_directory(argv[1], "files", "fr");
	test_number("open fr", error, 0);
	test_same("fr without catalogs", kl_tr("Move to Trash"), "Move to Trash");
	kl_tr_close();
	test_same("closed", kl_tr_language(), "en");

	/* The verdict. */
	if (test_failures != 0) {
		printf("tr-host: FAIL (%d)\n", test_failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("tr-host: PASS\n");
	return 0;
}

/*
 * Checks a text against the one expected.
 */
static void
test_same(
	const char *what,
	const char *got,
	const char *expected)
{
	int differs;

	/* The two texts. */
	differs = 1;
	if (got != NULL)
		differs = strcmp(got, expected);
	if (differs == 0) {
		printf("ok: %s\n", what);
		return;
	}

	/* A difference (no text at all shows as "(null)"). */
	if (got == NULL)
		got = "(null)";
	printf("FAIL: %s: got [%s] want [%s]\n", what, got, expected);
	test_failures++;
}

/*
 * Checks a number against the one expected.
 */
static void
test_number(
	const char *what,
	int got,
	int expected)
{
	/* The two numbers. */
	if (got == expected) {
		printf("ok: %s\n", what);
		return;
	}

	/* A difference. */
	printf("FAIL: %s: got %d want %d\n", what, got, expected);
	test_failures++;
}
