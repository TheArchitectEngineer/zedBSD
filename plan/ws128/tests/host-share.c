/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws128-p005: Image Viewer's Move to Trash and Open With (imageview/share.c, with Files' trash.c and apps.c) on the
 * host, in a temporary home (host-share.sh):
 *  1. iv_share_trash moves a picture into the home trash with its record; a second of the same name is "name.2".
 *  2. iv_share_trash on a missing file fails and leaves no record.
 *  3. iv_share_openers lists the user's open-with line for image/png ("My Paint") among Files' ways for the type.
 *  4. iv_share_open_with starts it on the picture (its command writes the path into a file).
 *  5. An index past the list is refused.
 */

#include "userland/desktop/imageview/imageview.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what);
static void make_file(const char *path, const char *text);
static int exists(const char *path);

int
main(
	int argc,
	char **argv)
{
	char path[1024];
	char other[1100];
	char trashed[2100];
	char names[IV_OPENERS][IV_OPENER_NAME];
	char text[256];
	const char *root;
	FILE *file;
	size_t length;
	int count;
	int found;
	int index;
	int error;

	if (argc != 2) {
		fprintf(stderr, "usage: host-share TEMPORARY-FOLDER\n");
		return 2;
	}
	root = argv[1];

	/* A home, its data and its configuration in the temporary folder. */
	snprintf(path, sizeof(path), "%s/home", root);
	mkdir(path, 0755);
	setenv("HOME", path, 1);
	snprintf(path, sizeof(path), "%s/home/.local/share", root);
	setenv("XDG_DATA_HOME", path, 1);
	snprintf(path, sizeof(path), "%s/home/.config", root);
	setenv("XDG_CONFIG_HOME", path, 1);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/home/.config/keiland", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/home/Pictures", root);
	mkdir(path, 0755);

	/* 1. A picture to the trash, then a second of the same name. */
	snprintf(path, sizeof(path), "%s/home/Pictures/Lake.png", root);
	make_file(path, "lake");
	error = iv_share_trash(path, trashed, sizeof(trashed));
	snprintf(other, sizeof(other), "%s/home/.local/share/Trash/files/Lake.png", root);
	check(error == 0 && strcmp(trashed, other) == 0 && exists(other) && !exists(path), "trash: the picture is in the home trash");
	snprintf(other, sizeof(other), "%s/home/.local/share/Trash/info/Lake.png.trashinfo", root);
	file = fopen(other, "r");
	length = 0;
	if (file != NULL) {
		length = fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
	}
	text[length] = '\0';
	snprintf(other, sizeof(other), "Path=%s/home/Pictures/Lake.png\n", root);
	check(strstr(text, other) != NULL, "trash: the record says where it was");
	make_file(path, "second");
	error = iv_share_trash(path, trashed, sizeof(trashed));
	snprintf(other, sizeof(other), "%s/home/.local/share/Trash/files/Lake.png.2", root);
	check(error == 0 && strcmp(trashed, other) == 0 && exists(other), "trash: a second of the same name is name.2");

	/* 2. A missing file. */
	snprintf(path, sizeof(path), "%s/home/Pictures/Missing.png", root);
	error = iv_share_trash(path, trashed, sizeof(trashed));
	snprintf(other, sizeof(other), "%s/home/.local/share/Trash/info/Missing.png.trashinfo", root);
	check(error == ENOENT && !exists(other), "trash: a missing file fails without a record");

	/* 3. The user's way for PNG pictures among the ways offered. */
	snprintf(path, sizeof(path), "%s/home/.config/keiland/open-with", root);
	snprintf(other, sizeof(other), "image/png\tMy Paint\techo %%f > %s/opened\n", root);
	make_file(path, other);
	snprintf(path, sizeof(path), "%s/home/Pictures/Boat.png", root);
	make_file(path, "boat");
	count = iv_share_openers(path, "PNG", names, IV_OPENERS);
	found = -1;
	for (index = 0; index < count; index++) {
		printf("opener %d %s\n", index, names[index]);
		if (strcmp(names[index], "My Paint") == 0 && found < 0)
			found = index;
	}
	check(count >= 1 && found >= 0, "open with: the user's way is offered");

	/* 4. It starts on the picture. */
	error = iv_share_open_with(path, "PNG", found);
	snprintf(other, sizeof(other), "%s/opened", root);
	for (index = 0; index < 50 && !exists(other); index++)
		usleep(20000);
	usleep(50000);
	file = fopen(other, "r");
	length = 0;
	if (file != NULL) {
		length = fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
	}
	text[length] = '\0';
	snprintf(other, sizeof(other), "%s\n", path);
	check(error == 0 && strcmp(text, other) == 0, "open with: the way started on the picture");

	/* 5. An index past the list. */
	error = iv_share_open_with(path, "PNG", IV_OPENERS);
	check(error == ENOENT, "open with: an index past the list is refused");

	printf("host-share: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}

/* Prints a check's outcome. */
static void
check(
	int condition,
	const char *what)
{
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		failures++;
}

/* Writes a file with a text. */
static void
make_file(
	const char *path,
	const char *text)
{
	FILE *file;

	file = fopen(path, "w");
	if (file == NULL)
		return;
	fputs(text, file);
	fclose(file);
}

/* Tells whether a path is there. */
static int
exists(
	const char *path)
{
	struct stat status;

	return lstat(path, &status) == 0;
}
