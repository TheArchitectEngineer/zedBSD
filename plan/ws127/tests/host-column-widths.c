/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-220 (q803): the host test of the keys Files keeps its list columns'
 * widths in (files.column-width.<column>, settings-keys.c): each key is
 * Files' own number, 0 to 2000, 0 by default; a width written goes to
 * files.conf and comes back, with the other lines of the file kept.
 */

#include "settings-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);

int
main(
	int argc,
	char **argv)
{
	static const char *const names[] = { "kind", "size", "modified", "changed", "owner", "location", "deleted" };
	const struct kl_settings_key *key;
	struct settings_cache cache;
	struct settings_app app;
	char home[512];
	char path[600];
	char value[KL_SETTINGS_VALUE_MAX];
	char text[256];
	size_t index;
	size_t length;
	FILE *file;
	char *made;
	int error;

	/* A home of its own under the folder given. */
	if (argc != 2)
		return 2;
	(void)snprintf(home, sizeof(home), "%s/home-XXXXXX", argv[1]);
	made = mkdtemp(home);
	if (made == NULL)
		return 1;

	/* Each column's key: Files' own number, 0 to 2000, 0 by default. */
	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		(void)snprintf(path, sizeof(path), "files.column-width.%s", names[index]);
		key = kl_settings_key_find(path);
		check(key != NULL && key->resolver == KL_SETTINGS_RESOLVER_APP && key->type == KL_SETTINGS_TYPE_INT && key->minimum == 0 && key->maximum == 2000 && key->fallback == 0, path);
	}

	/* The range: 180 is a width, 2001 and -1 are not. */
	key = kl_settings_key_find("files.column-width.size");
	check(key != NULL && kl_settings_key_check(key, "180") == 0, "range: 180 is taken");
	check(key != NULL && kl_settings_key_check(key, "2001") != 0, "range: 2001 is refused");
	check(key != NULL && kl_settings_key_check(key, "-1") != 0, "range: -1 is refused");

	/* Without a file, the default 0 (the column's own width). */
	error = settings_app_open(&app, "files", home);
	check(error == 0, "app: opens");
	settings_cache_init(&cache);
	settings_app_load(&app, &cache);
	error = settings_cache_get(&cache, "files.column-width.size", value, sizeof(value), NULL);
	check(error == 0 && strcmp(value, "0") == 0, "app: without a file, 0");

	/* A file with an opener's line, then a width written. */
	(void)snprintf(path, sizeof(path), "%s/.config", home);
	(void)mkdir(path, 0700);
	(void)snprintf(path, sizeof(path), "%s/.config/keiland", home);
	(void)mkdir(path, 0700);
	(void)snprintf(path, sizeof(path), "%s/.config/keiland/files.conf", home);
	file = fopen(path, "w");
	if (file == NULL)
		return 1;
	fputs("open-with.text/plain=Less\tless %f\n", file);
	fclose(file);
	error = settings_app_write(&app, "files.column-width.size", "180");
	check(error == 0, "write: the size column's width");
	error = settings_app_write(&app, "files.column-width.modified", "220");
	check(error == 0, "write: the modified column's width");

	/* Read again by another run: both widths, and the opener kept. */
	(void)settings_app_open(&app, "files", home);
	settings_cache_init(&cache);
	settings_app_load(&app, &cache);
	error = settings_cache_get(&cache, "files.column-width.size", value, sizeof(value), NULL);
	check(error == 0 && strcmp(value, "180") == 0, "read: 180 for the size column");
	error = settings_cache_get(&cache, "files.column-width.modified", value, sizeof(value), NULL);
	check(error == 0 && strcmp(value, "220") == 0, "read: 220 for the modified column");
	error = settings_cache_get(&cache, "files.column-width.kind", value, sizeof(value), NULL);
	check(error == 0 && strcmp(value, "0") == 0, "read: 0 for the kind column, never dragged");
	error = settings_cache_get(&cache, "files.open-with.text/plain", value, sizeof(value), NULL);
	check(error == 0 && strcmp(value, "Less\tless %f") == 0, "read: the opener's line is kept");

	/* The file's lines name the keys without the application's name. */
	file = fopen(path, "r");
	length = 0;
	if (file != NULL) {
		length = fread(text, 1, sizeof(text) - 1U, file);
		fclose(file);
	}

	/* The file's text, ended. */
	text[length] = '\0';
	check(strstr(text, "column-width.size=180\n") != NULL, "file: column-width.size=180");

	/* The summary. */
	printf("host-column-widths: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Counts and prints one check. */
static void
check(
	int ok,
	const char *what)
{
	/* A check that holds. */
	if (ok) {
		test_passed++;
		printf("ok   %s\n", what);
		return;
	}

	/* One that does not. */
	test_failed++;
	printf("FAIL %s\n", what);
}
