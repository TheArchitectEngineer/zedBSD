/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws093-p003: Always Open With on the host.  In a temporary
 * XDG_CONFIG_HOME, a user's list with lines of its own gets Files' default
 * for a type (fm_apps_set_default): the default comes first among the
 * type's ways, the user's lines stay, a second choice replaces the first
 * (one line a type), other types keep theirs, and Use System Default
 * (fm_apps_clear_default) takes only Files' line away.  Also a list made
 * where none was (the keiland folder made).
 *
 *   host-default TEMPORARY-FOLDER
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* How many checks failed; the program fails when any did. */
static int failures;

static void check(int condition, const char *text);
static int count_in(const char *path, const char *text);
static int first_is(const char *type, const char *name);

/* Runs the checks in a temporary folder. */
int
main(
	int argc,
	char **argv)
{
	char config[1024];
	char list[1200];
	struct fm_opener opener;
	FILE *file;
	int has;
	int error;

	/* The temporary folder is the configuration folder. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-default TEMPORARY-FOLDER\n");
		return 2;
	}
	snprintf(config, sizeof(config), "%s/config", argv[1]);
	setenv("XDG_CONFIG_HOME", config, 1);
	snprintf(list, sizeof(list), "%s/keiland/open-with", config);

	/* No list yet: no default, and a choice makes the folders and the list. */
	has = fm_apps_has_default("text/plain");
	check(has == 0, "no list: no default of the user's");
	memset(&opener, 0, sizeof(opener));
	snprintf(opener.name, sizeof(opener.name), "Terminal (less)");
	snprintf(opener.command, sizeof(opener.command), "@terminal less %%f");
	error = fm_apps_set_default("text/plain", &opener);
	check(error == 0, "no list: the choice made the list");
	check(first_is("text/plain", "Terminal (less)"), "no list: the chosen way is the default");
	has = fm_apps_has_default("text/plain");
	check(has == 1, "no list: the type has a default of the user's");

	/* The user's own lines, around Files' line. */
	file = fopen(list, "a");
	fprintf(file, "# my own\ntext/plain\tRecord\techo %%f\nimage/png\tViewer\tview %%f\n# set by Files\n");
	fclose(file);

	/* A second choice for the type replaces the first, and comes before the user's line. */
	snprintf(opener.name, sizeof(opener.name), "Remacs");
	snprintf(opener.command, sizeof(opener.command), "@terminal remacs %%f");
	error = fm_apps_set_default("text/plain", &opener);
	check(error == 0, "second choice written");
	check(first_is("text/plain", "Remacs"), "second choice: it is the default");
	check(count_in(list, "text/plain\t") == 2, "second choice: one line of Files' and the user's line for the type");
	check(count_in(list, "Terminal (less)") == 0, "second choice: the first choice is gone");
	check(count_in(list, "# my own") == 1, "second choice: the user's comment stays");
	check(count_in(list, "image/png\tViewer") == 1, "second choice: the user's line of another type stays");
	check(count_in(list, "# set by Files") == 2, "second choice: the comment of the new line, and the one the user left at the end");

	/* Another type's choice leaves the first type's. */
	snprintf(opener.name, sizeof(opener.name), "Quick Look");
	snprintf(opener.command, sizeof(opener.command), "@quicklook");
	error = fm_apps_set_default("image/png", &opener);
	check(error == 0, "another type written");
	check(first_is("image/png", "Quick Look"), "another type: its choice is its default, before the user's line");
	check(first_is("text/plain", "Remacs"), "another type: the first type keeps its choice");

	/* Use System Default takes only Files' line of the type away. */
	error = fm_apps_clear_default("text/plain");
	check(error == 0, "cleared");
	has = fm_apps_has_default("text/plain");
	check(has == 0, "cleared: no default of the user's");
	check(first_is("text/plain", "Record"), "cleared: the user's own line is the default again");
	check(first_is("image/png", "Quick Look"), "cleared: the other type keeps its choice");
	check(count_in(list, "Remacs") == 0, "cleared: Files' line is gone");

	/* Clearing a type without Files' line changes nothing. */
	error = fm_apps_clear_default("text/csv");
	check(error == 0, "clearing a type without a choice");
	check(count_in(list, "text/plain\tRecord") == 1, "clearing a type without a choice: the lines stay");

	/* The outcome. */
	if (failures != 0) {
		printf("host-default: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-default: PASS\n");
	return 0;
}

/* Prints a check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *text)
{
	/* A failed check is counted. */
	if (!condition) {
		printf("FAIL: %s\n", text);
		failures++;
		return;
	}

	/* A passed check is printed. */
	printf("ok: %s\n", text);
}

/* Counts the lines of a file that contain a text. */
static int
count_in(
	const char *path,
	const char *text)
{
	char line[1024];
	char *read;
	char *found;
	FILE *file;
	int count;

	/* The file; a missing one has no lines. */
	file = fopen(path, "r");
	if (file == NULL)
		return 0;

	/* Each line with the text. */
	count = 0;
	for (;;) {
		read = fgets(line, sizeof(line), file);
		if (read == NULL)
			break;

		/* A line with the text counts. */
		found = strstr(line, text);
		if (found != NULL)
			count++;
	}

	/* The file is done with. */
	fclose(file);

	/* Reports how many lines have the text. */
	return count;
}

/* Tells whether the first way of a type (a file of it that does not run) has a name. */
static int
first_is(
	const char *type,
	const char *name)
{
	struct fm_opener openers[FM_OPENERS];
	struct fm_mime mime;
	int count;
	int differs;

	/* The type's ways. */
	mime.type = type;
	mime.kind = "Test";
	mime.category = FM_CATEGORY_FILE;
	count = fm_apps_for("/x/file", &mime, 0100644, openers, FM_OPENERS);
	if (count < 1)
		return 0;

	/* The first way's name. */
	differs = strcmp(openers[0].name, name);
	if (differs != 0) {
		printf("  (first way of %s: %s)\n", type, openers[0].name);
		return 0;
	}

	/* The first way has the name. */
	return 1;
}
