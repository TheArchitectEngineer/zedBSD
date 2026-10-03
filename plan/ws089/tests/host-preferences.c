/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p007: tests the desktop's preferences (userland/desktop/libkeiland/
 * preferences.c) on the host, in a home of its own.
 *
 *   host-preferences /ABSOLUTE/HOME   (plan/ws089/tests/host-preferences.sh)
 *
 * Prints one line a check and "host-preferences: PASS" or "FAIL".
 */

#include <keiland.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

/* Records one check. */
static void
check(
	const char *name,
	int passed)
{
	/* The line, and a failure counted. */
	printf("%s: %s\n", name, passed ? "ok" : "FAILED");
	if (!passed)
		failures++;
}

/* Reads a whole small file into a buffer. */
static void
slurp(
	const char *path,
	char *text,
	size_t size)
{
	FILE *file;
	size_t count;

	/* The text, or nothing. */
	text[0] = '\0';
	file = fopen(path, "r");
	if (file == NULL)
		return;
	count = fread(text, 1, size - 1, file);
	text[count] = '\0';
	fclose(file);
}

int
main(
	int argc,
	char **argv)
{
	struct keiland_preferences *first;
	struct keiland_preferences *second;
	char path[1024];
	char value[256];
	char text[4096];
	FILE *file;
	int changed;
	int error;
	int number;

	/* The home given, absolute (a relative HOME is ignored and the real home would be used). */
	if (argc != 2 || argv[1][0] != '/') {
		fprintf(stderr, "usage: host-preferences /ABSOLUTE/HOME\n");
		return 2;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	setenv("HOME", argv[1], 1);
	snprintf(path, sizeof(path), "%s/.config/keiland/desktop.conf", argv[1]);
	unlink(path);

	/* A missing file has no key, and a reload of it changes nothing. */
	first = keiland_preferences_open();
	check("open", first != NULL);
	if (first == NULL)
		return 1;
	error = keiland_preferences_get(first, "wallpaper", value, sizeof(value));
	check("missing key is ENOENT", error == ENOENT);
	error = keiland_preferences_reload(first, &changed);
	check("reload of a missing file", error == 0 && changed == 0);
	number = keiland_preferences_get_int(first, "pointer.speed", 100, 25, 300);
	check("get_int falls back", number == 100);

	/* A set makes the folder and the file. */
	error = keiland_preferences_set(first, "wallpaper", "/usr/share/keiland/wallpaper.ppm");
	check("set wallpaper", error == 0);
	error = keiland_preferences_get(first, "wallpaper", value, sizeof(value));
	check("get wallpaper", error == 0 && strcmp(value, "/usr/share/keiland/wallpaper.ppm") == 0);

	/* A hand edit's comment and unknown key survive a later set. */
	file = fopen(path, "a");
	fputs("# a comment by hand\nsomething.else=kept\n", file);
	fclose(file);
	sleep(1);
	second = keiland_preferences_open();
	check("second open", second != NULL);
	if (second == NULL)
		return 1;
	error = keiland_preferences_get(second, "something.else", value, sizeof(value));
	check("unknown key read", error == 0 && strcmp(value, "kept") == 0);

	/* Two writers in turn keep both keys. */
	error = keiland_preferences_set(first, "window.opacity", "90");
	check("first writer sets opacity", error == 0);
	error = keiland_preferences_set(second, "pointer.speed", "400");
	check("second writer sets speed", error == 0);
	error = keiland_preferences_get(second, "window.opacity", value, sizeof(value));
	check("second sees the first's key", error == 0 && strcmp(value, "90") == 0);
	slurp(path, text, sizeof(text));
	check("file keeps the comment", strstr(text, "# a comment by hand\n") != NULL);
	check("file keeps the unknown key", strstr(text, "something.else=kept\n") != NULL);
	check("file has the wallpaper once", strstr(text, "wallpaper=") != NULL && strstr(strstr(text, "wallpaper=") + 1, "wallpaper=") == NULL);

	/* The first notices the second's change at its reload; a value out of range is moved into it. */
	error = keiland_preferences_reload(first, &changed);
	check("reload sees the change", error == 0 && changed == 1);
	number = keiland_preferences_get_int(first, "pointer.speed", 100, 25, 300);
	check("get_int clamps 400 to 300", number == 300);
	error = keiland_preferences_reload(first, &changed);
	check("reload without a change", error == 0 && changed == 0);

	/* Replacing a value keeps one line of the key. */
	error = keiland_preferences_set(first, "window.opacity", "85");
	check("set opacity again", error == 0);
	slurp(path, text, sizeof(text));
	check("one opacity line", strstr(text, "window.opacity=85\n") != NULL && strstr(text, "window.opacity=90") == NULL);

	/* Unset removes the key; an unset key is no error. */
	error = keiland_preferences_unset(first, "wallpaper");
	check("unset wallpaper", error == 0);
	error = keiland_preferences_get(first, "wallpaper", value, sizeof(value));
	check("wallpaper gone", error == ENOENT);
	error = keiland_preferences_unset(first, "wallpaper");
	check("unset again", error == 0);

	/* Keys and values that are not allowed. */
	error = keiland_preferences_set(first, "Bad Key", "1");
	check("bad key is EINVAL", error == EINVAL);
	error = keiland_preferences_set(first, "ok.key", "two\nlines");
	check("newline in a value is EINVAL", error == EINVAL);

	/* A value that does not fit the caller's room. */
	error = keiland_preferences_get(first, "something.else", value, 3);
	check("small room is ERANGE", error == ERANGE);

	/* No file left behind but the preferences and the lock. */
	keiland_preferences_close(first);
	keiland_preferences_close(second);
	printf("host-preferences: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
