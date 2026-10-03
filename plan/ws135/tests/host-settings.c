/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws135-p003: the host tests of libkeiland's settings cache
 * (userland/desktop/libkeiland/settings-cache.c) and of an application's
 * own settings file (settings-app.c): the first state and its done, the
 * watches (one call a key with its last value, none for a value that
 * changed back, a watch added or stopped from within a callback, prefixes),
 * the compositor going, the results, and the application's file (defaults,
 * values read, a change written with the other lines kept, a reset).
 */

#include "settings-private.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_passed;
static int test_failed;
static char test_home[256];

/* What the watches heard. */
static char heard[16][96];
static unsigned heard_count;
static struct settings_cache *test_cache;
static unsigned added_watch;
static unsigned stop_watch;

static void check(int ok, const char *format, ...);
static void listen(void *data, const char *key, const char *value, unsigned flags);
static void listen_adding(void *data, const char *key, const char *value, unsigned flags);
static void listen_stopping(void *data, const char *key, const char *value, unsigned flags);
static void test_state(void);
static void test_watch_changes(void);
static void test_lost(void);
static void test_results(void);
static void test_app(void);

int
main(
	void)
{
	char *made;

	snprintf(test_home, sizeof(test_home), "/tmp/ws135-host-settings-XXXXXX");
	made = mkdtemp(test_home);
	if (made == NULL)
		return 1;
	test_state();
	test_watch_changes();
	test_lost();
	test_results();
	test_app();
	printf("host-settings: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

static void
check(
	int ok,
	const char *format,
	...)
{
	va_list arguments;

	if (ok) {
		test_passed++;
		printf("ok   ");
	} else {
		test_failed++;
		printf("FAIL ");
	}
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

static void
listen(
	void *data,
	const char *key,
	const char *value,
	unsigned flags)
{
	(void)data;
	if (heard_count < 16U) {
		snprintf(heard[heard_count], sizeof(heard[0]), "%s=%s/%u", key, value != NULL ? value : "(none)", flags);
		heard_count++;
	}
}

static void
listen_adding(
	void *data,
	const char *key,
	const char *value,
	unsigned flags)
{
	listen(data, key, value, flags);
	if (added_watch == 0U)
		(void)settings_cache_watch(test_cache, "", listen, NULL, &added_watch);
}

static void
listen_stopping(
	void *data,
	const char *key,
	const char *value,
	unsigned flags)
{
	listen(data, key, value, flags);
	settings_cache_unwatch(test_cache, stop_watch);
}

static void
test_state(void)
{
	struct settings_cache cache;
	char value[KL_SETTINGS_VALUE_MAX];
	unsigned flags;
	int error;

	settings_cache_init(&cache);
	error = settings_cache_get(&cache, "pointer.speed", value, sizeof(value), &flags);
	check(error == EAGAIN, "state: no value before the compositor's (got %d)", error);
	settings_cache_pending(&cache, "pointer.speed", "120", 0U, 1U);
	error = settings_cache_get(&cache, "pointer.speed", value, sizeof(value), &flags);
	check(error == EAGAIN, "state: a value is not seen before its done");
	settings_cache_done(&cache);
	error = settings_cache_get(&cache, "pointer.speed", value, sizeof(value), &flags);
	check(error == 0 && strcmp(value, "120") == 0 && flags == 0U, "state: in effect after the done");
	check(settings_cache_get(&cache, "no.key", value, sizeof(value), &flags) == ENOENT, "state: an unknown key is ENOENT");
	check(settings_cache_get(&cache, "pointer.speed", value, 3, &flags) == ERANGE, "state: too small room is ERANGE");
	settings_cache_pending(&cache, "newer.compositor.key", "1", 0U, 1U);
	settings_cache_done(&cache);
	check(1, "state: a key the table does not have is passed over");
}

static void
test_watch_changes(void)
{
	struct settings_cache cache;
	unsigned all;
	unsigned pointer;
	unsigned adder;

	settings_cache_init(&cache);
	test_cache = &cache;
	settings_cache_pending(&cache, "pointer.speed", "100", KL_SETTINGS_DEFAULT, 1U);
	settings_cache_pending(&cache, "window.opacity", "100", KL_SETTINGS_DEFAULT, 1U);
	settings_cache_done(&cache);
	settings_cache_settle(&cache);
	(void)settings_cache_watch(&cache, "", listen, NULL, &all);
	(void)settings_cache_watch(&cache, "pointer.", listen, NULL, &pointer);
	check(settings_cache_watch(&cache, "", NULL, NULL, NULL) == EINVAL, "watch: a watch without a callback is EINVAL");

	/* The first state is not told. */
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 0U, "watch: the start is not told (%u)", heard_count);

	/* One change, heard by both watches; the other key only by the first. */
	settings_cache_pending(&cache, "pointer.speed", "150", 0U, 1U);
	settings_cache_done(&cache);
	settings_cache_pending(&cache, "pointer.speed", "160", 0U, 1U);
	settings_cache_pending(&cache, "window.opacity", "90", 0U, 1U);
	settings_cache_done(&cache);
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 3U && strcmp(heard[1], "pointer.speed=160/0") == 0 && strcmp(heard[2], "pointer.speed=160/0") == 0 && strcmp(heard[0], "window.opacity=90/0") == 0,
	      "watch: two dones, one call a watch with the last value, in the table's order (%u, %s)", heard_count, heard[1]);

	/* A value that changed and changed back is not told. */
	settings_cache_pending(&cache, "pointer.speed", "170", 0U, 1U);
	settings_cache_done(&cache);
	settings_cache_pending(&cache, "pointer.speed", "160", 0U, 1U);
	settings_cache_done(&cache);
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 0U, "watch: A to B to A is not told (%u)", heard_count);

	/* A watch stopped is not called; one added in a callback hears the next change only. */
	settings_cache_unwatch(&cache, pointer);
	(void)settings_cache_watch(&cache, "window.", listen_adding, NULL, &adder);
	added_watch = 0;
	settings_cache_pending(&cache, "window.opacity", "95", 0U, 1U);
	settings_cache_done(&cache);
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 2U && added_watch != 0U, "watch: the stopped watch is not called, the added one not yet (%u)", heard_count);
	settings_cache_pending(&cache, "window.opacity", "96", 0U, 1U);
	settings_cache_done(&cache);
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 3U, "watch: the added watch hears the next change (%u)", heard_count);

	/* A watch that stops another from within its callback: the other is not called. */
	settings_cache_unwatch(&cache, adder);
	settings_cache_unwatch(&cache, added_watch);
	settings_cache_unwatch(&cache, all);
	(void)settings_cache_watch(&cache, "", listen_stopping, NULL, NULL);
	(void)settings_cache_watch(&cache, "", listen, NULL, &stop_watch);
	settings_cache_pending(&cache, "window.opacity", "97", 0U, 1U);
	settings_cache_done(&cache);
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 1U, "watch: a watch stopped from a callback is not called afterwards (%u)", heard_count);
}

static void
test_lost(void)
{
	struct settings_cache cache;
	char value[KL_SETTINGS_VALUE_MAX];
	unsigned all;

	settings_cache_init(&cache);
	settings_cache_pending(&cache, "pointer.speed", "100", 0U, 1U);
	settings_cache_done(&cache);
	settings_cache_set(&cache, "terminal.ambiguous-wide", "1", 0U, 1U);
	settings_cache_settle(&cache);
	(void)settings_cache_watch(&cache, "", listen, NULL, &all);
	settings_cache_lost(&cache, KL_SETTINGS_RESOLVER_COMPOSITOR);
	heard_count = 0;
	settings_cache_notify(&cache);
	check(heard_count == 1U && strcmp(heard[0], "pointer.speed=(none)/0") == 0, "lost: the compositor's key is told without a value (%u)", heard_count);
	check(settings_cache_get(&cache, "terminal.ambiguous-wide", value, sizeof(value), NULL) == 0, "lost: the application's key stays");
}

static void
test_results(void)
{
	struct settings_cache cache;
	uint32_t request;
	unsigned index;
	int error;
	int taken;

	settings_cache_init(&cache);
	check(settings_cache_take_result(&cache, &request, &error) == 0, "results: none at first");
	settings_cache_result(&cache, 7, 0);
	settings_cache_result(&cache, 8, EBUSY);
	taken = settings_cache_take_result(&cache, &request, &error);
	check(taken == 1 && request == 7 && error == 0, "results: the oldest first");
	taken = settings_cache_take_result(&cache, &request, &error);
	check(taken == 1 && request == 8 && error == EBUSY, "results: then the next");
	for (index = 0; index < SETTINGS_CACHE_RESULTS + 3U; index++)
		settings_cache_result(&cache, 100U + index, 0);
	taken = settings_cache_take_result(&cache, &request, &error);
	check(taken == 1 && request == 103U, "results: a full ring drops its oldest (%u)", request);
}

static void
test_app(void)
{
	struct settings_cache cache;
	struct settings_app app;
	char path[512];
	char value[KL_SETTINGS_VALUE_MAX];
	char text[256];
	unsigned flags;
	FILE *file;
	size_t length;
	int error;

	/* No file: the default. */
	error = settings_app_open(&app, "terminal", test_home);
	check(error == 0, "app: opens");
	check(settings_app_open(&app, "Bad Name", test_home) == EINVAL, "app: a bad name is EINVAL");
	(void)settings_app_open(&app, "terminal", test_home);
	settings_cache_init(&cache);
	settings_app_load(&app, &cache);
	error = settings_cache_get(&cache, "terminal.ambiguous-wide", value, sizeof(value), &flags);
	check(error == 0 && strcmp(value, "0") == 0 && flags == KL_SETTINGS_DEFAULT, "app: without a file, the default 0");

	/* A file of the terminal's with another line. */
	snprintf(path, sizeof(path), "%s/.config", test_home);
	(void)mkdir(path, 0700);
	snprintf(path, sizeof(path), "%s/.config/keiland", test_home);
	(void)mkdir(path, 0700);
	snprintf(path, sizeof(path), "%s/.config/keiland/terminal.conf", test_home);
	file = fopen(path, "w");
	if (file == NULL)
		return;
	fputs("# terminal\nambiguous-wide=1\nother=x\n", file);
	fclose(file);
	settings_cache_init(&cache);
	settings_app_load(&app, &cache);
	error = settings_cache_get(&cache, "terminal.ambiguous-wide", value, sizeof(value), &flags);
	check(error == 0 && strcmp(value, "1") == 0 && flags == 0U, "app: the file's 1 is read");

	/* A change keeps the other lines; a reset takes the line out. */
	error = settings_app_write(&app, "terminal.ambiguous-wide", "0");
	file = fopen(path, "r");
	length = file != NULL ? fread(text, 1, sizeof(text) - 1U, file) : 0;
	text[length] = '\0';
	if (file != NULL)
		fclose(file);
	check(error == 0 && strstr(text, "ambiguous-wide=0\n") != NULL && strstr(text, "other=x\n") != NULL && strstr(text, "# terminal\n") != NULL,
	      "app: a change is written, the other lines kept");
	error = settings_app_write(&app, "terminal.ambiguous-wide", NULL);
	file = fopen(path, "r");
	length = file != NULL ? fread(text, 1, sizeof(text) - 1U, file) : 0;
	text[length] = '\0';
	if (file != NULL)
		fclose(file);
	check(error == 0 && strstr(text, "ambiguous-wide") == NULL && strstr(text, "other=x\n") != NULL, "app: a reset takes the line out");
	check(settings_app_write(&app, "pointer.speed", "1") == EINVAL, "app: another key is not the application's");
}
