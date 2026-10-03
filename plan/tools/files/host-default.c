/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws093-p003, rewritten for WS135 (ws135-p005): Always Open With on the
 * host.  In a temporary HOME and XDG_CONFIG_HOME, the way chosen for a type
 * (fm_apps_set_default) is the desktop's setting files.open-with.<type>,
 * which libkeiland keeps in ~/.config/keiland/files.conf: the choice comes
 * first among the type's ways, before the user's own list's lines (which
 * Files only reads), a second choice replaces the first, other types keep
 * theirs, Use System Default (fm_apps_clear_default) takes only the choice
 * away, and a line an earlier Files wrote into the user's list after its
 * "# set by Files" comment is passed over.
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
	char folder[1100];
	char list[1200];
	char settings[1200];
	struct fm_opener opener;
	FILE *file;
	int has;
	int error;

	/* The temporary folder is the home and holds the configuration folder. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-default TEMPORARY-FOLDER\n");
		return 2;
	}

	/* The configuration folder under it, where the user's list is, and the settings' file. */
	setenv("HOME", argv[1], 1);
	snprintf(config, sizeof(config), "%s/config", argv[1]);
	setenv("XDG_CONFIG_HOME", config, 1);
	snprintf(list, sizeof(list), "%s/keiland/open-with", config);
	snprintf(settings, sizeof(settings), "%s/.config/keiland/files.conf", argv[1]);

	/* No choice yet: no default, and a choice is the setting. */
	has = fm_apps_has_default("text/plain");
	check(has == 0, "no choice: no default of the user's");
	memset(&opener, 0, sizeof(opener));
	snprintf(opener.name, sizeof(opener.name), "Terminal (less)");
	snprintf(opener.command, sizeof(opener.command), "@terminal less %%f");
	error = fm_apps_set_default("text/plain", &opener);
	check(error == 0, "first choice: set");
	check(first_is("text/plain", "Terminal (less)"), "first choice: the chosen way is the default");
	has = fm_apps_has_default("text/plain");
	check(has == 1, "first choice: the type has a default of the user's");
	check(count_in(settings, "open-with.text/plain=Terminal (less)\t@terminal less %f") == 1, "first choice: files.conf holds it");
	check(count_in(list, "Terminal (less)") == 0, "first choice: the user's list is not written");

	/* The user's own lines, and a line an earlier Files wrote. */
	mkdir(config, 0700);
	snprintf(folder, sizeof(folder), "%s/keiland", config);
	mkdir(folder, 0700);
	file = fopen(list, "w");
	fprintf(file, "# my own\n# set by Files\ntext/plain\tOld choice\told %%f\ntext/plain\tRecord\techo %%f\nimage/png\tViewer\tview %%f\n");
	fclose(file);

	/* A second choice for the type replaces the first, and comes before the user's line. */
	snprintf(opener.name, sizeof(opener.name), "Remacs");
	snprintf(opener.command, sizeof(opener.command), "@terminal remacs %%f");
	error = fm_apps_set_default("text/plain", &opener);
	check(error == 0, "second choice: set");
	check(first_is("text/plain", "Remacs"), "second choice: it is the default");
	check(count_in(settings, "open-with.text/plain=") == 1, "second choice: one line of the type in files.conf");
	check(count_in(settings, "Terminal (less)") == 0, "second choice: the first choice is gone");
	check(count_in(list, "# my own") == 1 && count_in(list, "image/png\tViewer") == 1, "second choice: the user's list stays as it was");

	/* Another type's choice leaves the first type's. */
	snprintf(opener.name, sizeof(opener.name), "Quick Look");
	snprintf(opener.command, sizeof(opener.command), "@quicklook");
	error = fm_apps_set_default("image/png", &opener);
	check(error == 0, "another type: set");
	check(first_is("image/png", "Quick Look"), "another type: its choice is its default, before the user's line");
	check(first_is("text/plain", "Remacs"), "another type: the first type keeps its choice");

	/* Use System Default takes only the choice away; the earlier Files' line stays passed over. */
	error = fm_apps_clear_default("text/plain");
	check(error == 0, "cleared");
	has = fm_apps_has_default("text/plain");
	check(has == 0, "cleared: no default of the user's");
	check(first_is("text/plain", "Record"), "cleared: the user's own line is the default (the earlier Files' line is passed over)");
	check(first_is("image/png", "Quick Look"), "cleared: the other type keeps its choice");
	check(count_in(settings, "Remacs") == 0, "cleared: the choice left files.conf");

	/* Clearing a type without a choice changes nothing; a type the setting cannot name is refused. */
	error = fm_apps_clear_default("text/csv");
	check(error == 0, "clearing a type without a choice");
	error = fm_apps_set_default("Bad Type", &opener);
	check(error != 0, "a type that is not a MIME type is refused");

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
