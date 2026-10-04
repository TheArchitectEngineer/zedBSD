/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of About's version name (ws089-p027): se_about_pretty_name
 * of userland/desktop/settings/about.c on os-release files the test
 * writes: zedBSD's own (PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"), single
 * quotes, no quotes, backslash escapes, a comment and a key that only
 * begins like it, a file without PRETTY_NAME, an empty one, a missing
 * file, and a name cut short to the buffer.
 *
 *   sh plan/ws089/tests/host-about.sh
 */

#include "settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void check(int condition, const char *what);
static int name_of(const char *text, char *name, size_t size);

/* The checks that failed, and those that ran; the file the test writes. */
static int failures;
static int checks;
static char path[256];

/* The log is not checked. */
void
se_log(
	const char *format,
	...)
{
	/* Nothing to write. */
	(void)format;
}

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Writes an os-release file and reads its name. */
static int
name_of(
	const char *text,
	char *name,
	size_t size)
{
	FILE *file;
	int result;

	/* The file. */
	file = fopen(path, "w");
	if (file == NULL)
		return -2;
	(void)fputs(text, file);
	(void)fclose(file);

	/* Its name. */
	result = se_about_pretty_name(path, name, size);
	return result;
}

int
main(
	int argc,
	char **argv)
{
	char name[128];
	char small[8];
	int result;

	/* The build directory is given. */
	if (argc < 2) {
		fprintf(stderr, "usage: host-about DIRECTORY\n");
		return 2;
	}

	/* The file beside the test. */
	(void)snprintf(path, sizeof(path), "%s/os-release", argv[1]);

	/* zedBSD's own file (the top-level Makefile's lines). */
	result = name_of("NAME=\"zedBSD\"\nID=zedbsd\nVERSION=\"1.0.0 Beta 1\"\nVERSION_ID=1.0.0-beta1\n"
			 "PRETTY_NAME=\"Kei/zedBSD 1.0.0 Beta 1\"\nBUILD_ID=g1234567\n", name, sizeof(name));
	check(result == 0 && strcmp(name, "Kei/zedBSD 1.0.0 Beta 1") == 0, "zedBSD's PRETTY_NAME");

	/* Single quotes, no quotes, escapes. */
	result = name_of("PRETTY_NAME='Debian GNU/Linux 13 (trixie)'\n", name, sizeof(name));
	check(result == 0 && strcmp(name, "Debian GNU/Linux 13 (trixie)") == 0, "single quotes");
	result = name_of("PRETTY_NAME=Plain\n", name, sizeof(name));
	check(result == 0 && strcmp(name, "Plain") == 0, "no quotes");
	result = name_of("PRETTY_NAME=\"A \\\"quoted\\\" \\\\ name\"\n", name, sizeof(name));
	check(result == 0 && strcmp(name, "A \"quoted\" \\ name") == 0, "backslash escapes");

	/* A comment and a key that only begins like it come before it. */
	result = name_of("# PRETTY_NAME=\"no\"\nPRETTY_NAMES=\"no\"\nPRETTY_NAME=\"yes\"\r\n", name, sizeof(name));
	check(result == 0 && strcmp(name, "yes") == 0, "comment and a longer key skipped, CR dropped");

	/* No PRETTY_NAME, an empty one, an empty file, no file. */
	result = name_of("NAME=\"zedBSD\"\n", name, sizeof(name));
	check(result == -1 && name[0] == '\0', "no PRETTY_NAME");
	result = name_of("PRETTY_NAME=\"\"\n", name, sizeof(name));
	check(result == -1 && name[0] == '\0', "an empty PRETTY_NAME counts as none");
	result = name_of("", name, sizeof(name));
	check(result == -1 && name[0] == '\0', "an empty file");
	(void)remove(path);
	result = se_about_pretty_name(path, name, sizeof(name));
	check(result == -1 && name[0] == '\0', "no file");

	/* Cut short to the buffer. */
	result = name_of("PRETTY_NAME=\"Kei/zedBSD 1.0.0 Beta 1\"\n", small, sizeof(small));
	check(result == 0 && strcmp(small, "Kei/zed") == 0, "cut short to fit");
	(void)remove(path);

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-about: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-about: %d checks passed\n", checks);
	return 0;
}
