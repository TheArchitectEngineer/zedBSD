/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * An application's own settings, which libkeiland resolves itself (WS135
 * D3 (a), plan/ws135/design.md section 2.3): the keys <app>.<name> are the
 * lines <name>=<value> of ~/.config/keiland/<app>.conf.
 *
 * The file is read when the settings open, and a change rewrites it with
 * that line replaced (or taken out, for a key back at its default) and
 * every other line kept, into a new file beside it that is flushed to the
 * disk and renamed over the old one: an application starting at the same
 * time never reads half a file.  Another process of the application reads
 * the file when it starts; it is not told of the change.
 */

#include "settings-private.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The largest file read or written, in bytes. */
#define APP_FILE_MAX		16384U

/* The files' folder under the home. */
#define APP_FOLDER		".config/keiland"

static void app_parse_line(const struct settings_app *app, struct settings_cache *cache, const char *line, size_t length);
static int app_read(const char *path, char *text, size_t capacity, size_t *length);
static int app_compose(const char *old_text, size_t old_length, const char *name, const char *value, char *text, size_t capacity, size_t *length);
static int app_append(char *text, size_t capacity, size_t *length, const char *part, size_t part_length);
static int app_replace(const char *path, const char *text, size_t length);

/*
 * Names an application's file in a home.  Returns 0, EINVAL for a name
 * that is not well formed or too long, ENOENT without a home, or
 * ENAMETOOLONG.
 */
int
settings_app_open(
	struct settings_app *app,
	const char *name,
	const char *home)
{
	size_t length;
	int written;
	int valid;

	memset(app, 0, sizeof(*app));

	/* A well-formed name that fits. */
	valid = kl_settings_name_valid(name);
	if (!valid)
		return EINVAL;
	length = strlen(name);
	if (length >= sizeof(app->name))
		return EINVAL;

	/* A home. */
	if (home == NULL || home[0] != '/')
		return ENOENT;

	/* The folder and the file. */
	written = snprintf(app->folder, sizeof(app->folder), "%s/%s", home, APP_FOLDER);
	if (written < 0 || (size_t)written >= sizeof(app->folder))
		return ENAMETOOLONG;
	written = snprintf(app->path, sizeof(app->path), "%s/%s.conf", app->folder, name);
	if (written < 0 || (size_t)written >= sizeof(app->path))
		return ENAMETOOLONG;

	/* Succeeded: the file is the application's. */
	memcpy(app->name, name, length + 1U);
	app->used = 1;
	return 0;
}

/*
 * Puts the application's keys into the cache: each at its default, then
 * the file's values over them.
 */
void
settings_app_load(
	const struct settings_app *app,
	struct settings_cache *cache)
{
	const struct kl_settings_key *key;
	char fallback[16];
	char *text;
	size_t prefix_length;
	size_t length;
	size_t count;
	size_t index;
	size_t start;
	size_t end;
	int differs;
	int error;

	/* No application, no keys. */
	if (!app->used)
		return;

	/* Each key of the application starts at its default. */
	prefix_length = strlen(app->name);
	count = kl_settings_key_count();
	for (index = 0; index < count; index++) {
		key = kl_settings_key_at(index);
		if (key->resolver != KL_SETTINGS_RESOLVER_APP)
			continue;
		differs = strncmp(key->name, app->name, prefix_length);
		if (differs != 0 || key->name[prefix_length] != '.')
			continue;
		(void)snprintf(fallback, sizeof(fallback), "%d", key->fallback);
		settings_cache_set(cache, key->name, fallback, KL_SETTINGS_DEFAULT, 1U);
	}

	/* The file's text; a missing or unreadable file leaves the defaults. */
	text = malloc(APP_FILE_MAX);
	if (text == NULL)
		return;
	error = app_read(app->path, text, APP_FILE_MAX, &length);
	if (error != 0) {
		free(text);
		return;
	}

	/* Each line, the last one with or without its newline. */
	start = 0;
	while (start < length) {
		/* The line's end. */
		end = start;
		while (end < length && text[end] != '\n')
			end++;

		/* Its key, if it is a line of one. */
		app_parse_line(app, cache, text + start, end - start);
		start = end + 1U;
	}

	free(text);
}

/*
 * Writes one of the application's keys into its file: value, or NULL to
 * take the key out (its default).  Returns 0, EINVAL, E2BIG or an errno
 * value of the file.
 */
int
settings_app_write(
	const struct settings_app *app,
	const char *key,
	const char *value)
{
	const char *name;
	char *before;
	char *after;
	size_t prefix_length;
	size_t before_length;
	size_t after_length;
	int differs;
	int error;

	/* Only the application's own key. */
	if (!app->used)
		return ENOENT;
	prefix_length = strlen(app->name);
	differs = strncmp(key, app->name, prefix_length);
	if (differs != 0 || key[prefix_length] != '.')
		return EINVAL;
	name = key + prefix_length + 1U;

	/* Room for the file as it is and as it is to be. */
	before = malloc(APP_FILE_MAX);
	if (before == NULL)
		return ENOMEM;
	after = malloc(APP_FILE_MAX);
	if (after == NULL) {
		free(before);
		return ENOMEM;
	}

	/* The folder, the file as it is, the line changed, and the new file. */
	(void)mkdir(app->folder, 0700);
	error = app_read(app->path, before, APP_FILE_MAX, &before_length);
	if (error == 0)
		error = app_compose(before, before_length, name, value, after, APP_FILE_MAX, &after_length);
	if (error == 0)
		error = app_replace(app->path, after, after_length);
	free(after);
	free(before);

	/* Reports why the file could not be changed. */
	if (error != 0)
		return error;

	/* Succeeded: the file holds the change. */
	return 0;
}

/* Takes a name=value line's key into the cache when it is one of the application's, of its type and range. */
static void
app_parse_line(
	const struct settings_app *app,
	struct settings_cache *cache,
	const char *line,
	size_t length)
{
	const struct kl_settings_key *key;
	char full[KL_SETTINGS_KEY_MAX];
	char raw[KL_SETTINGS_VALUE_MAX];
	char clean[KL_SETTINGS_VALUE_MAX];
	const char *equals;
	size_t name_length;
	size_t value_length;
	int number;
	int written;
	int error;

	/* An empty line and a comment hold no key. */
	if (length == 0U || line[0] == '#')
		return;

	/* The name before the first '=' and the value after it, both fitting. */
	equals = memchr(line, '=', length);
	if (equals == NULL)
		return;
	name_length = (size_t)(equals - line);
	value_length = length - name_length - 1U;
	if (name_length == 0U || value_length >= sizeof(raw))
		return;
	written = snprintf(full, sizeof(full), "%s.%.*s", app->name, (int)name_length, line);
	if (written < 0 || (size_t)written >= sizeof(full))
		return;
	memcpy(raw, equals + 1, value_length);
	raw[value_length] = '\0';

	/* A key of the table's, the application's. */
	key = kl_settings_key_find(full);
	if (key == NULL || key->resolver != KL_SETTINGS_RESOLVER_APP)
		return;

	/* A number moved into its range. */
	error = kl_settings_key_number(key, raw, &number);
	if (error != 0)
		return;
	(void)snprintf(clean, sizeof(clean), "%d", number);

	/* The value in effect. */
	settings_cache_set(cache, full, clean, 0U, 1U);
}

/* Reads a file's whole text (a missing file is empty); returns 0, E2BIG or an errno value. */
static int
app_read(
	const char *path,
	char *text,
	size_t capacity,
	size_t *length)
{
	ssize_t count;
	int descriptor;
	int error;

	/* Nothing yet. */
	*length = 0;

	/* The file, when there is one. */
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		if (errno == ENOENT)
			return 0;
		return errno;
	}

	/* Every byte, up to the room there is. */
	error = 0;
	for (;;) {
		/* A file that fills the room is too large. */
		if (*length == capacity) {
			error = E2BIG;
			break;
		}

		/* The next bytes; none left is the end. */
		count = read(descriptor, text + *length, capacity - *length);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			error = errno;
			break;
		}
		if (count == 0)
			break;
		*length += (size_t)count;
	}

	/* The file is not needed any more. */
	(void)close(descriptor);

	/* Reports why the text could not be read. */
	if (error != 0)
		return error;

	/* Succeeded: the text is read. */
	return 0;
}

/* Writes a text with one name set (value) or taken out (NULL), every other line kept; returns 0 or E2BIG. */
static int
app_compose(
	const char *old_text,
	size_t old_length,
	const char *name,
	const char *value,
	char *text,
	size_t capacity,
	size_t *length)
{
	size_t name_length;
	size_t start;
	size_t end;
	int written;
	int matched;
	int differs;
	int error;

	/* Nothing written yet, and the name not yet met. */
	*length = 0;
	written = 0;
	error = 0;
	name_length = strlen(name);

	/* Each line of the old text. */
	start = 0;
	while (error == 0 && start < old_length) {
		/* The line's end. */
		end = start;
		while (end < old_length && old_text[end] != '\n')
			end++;

		/* The name's line: its first becomes the new value (or goes), the others go. */
		matched = 0;
		if (end - start > name_length) {
			differs = memcmp(old_text + start, name, name_length);
			if (differs == 0 && old_text[start + name_length] == '=')
				matched = 1;
		}
		if (matched) {
			if (!written && value != NULL) {
				error = app_append(text, capacity, length, name, name_length);
				if (error == 0)
					error = app_append(text, capacity, length, "=", 1U);
				if (error == 0)
					error = app_append(text, capacity, length, value, strlen(value));
				if (error == 0)
					error = app_append(text, capacity, length, "\n", 1U);
			}
			written = 1;
		} else {
			/* Any other line stays as it was. */
			error = app_append(text, capacity, length, old_text + start, end - start);
			if (error == 0)
				error = app_append(text, capacity, length, "\n", 1U);
		}

		/* The next line. */
		start = end + 1U;
	}

	/* A name the file did not have goes at its end. */
	if (error == 0 && !written && value != NULL) {
		error = app_append(text, capacity, length, name, name_length);
		if (error == 0)
			error = app_append(text, capacity, length, "=", 1U);
		if (error == 0)
			error = app_append(text, capacity, length, value, strlen(value));
		if (error == 0)
			error = app_append(text, capacity, length, "\n", 1U);
	}

	/* Reports a text too large. */
	if (error != 0)
		return error;

	/* Succeeded: the new text. */
	return 0;
}

/* Adds a part to a text; returns 0, or E2BIG when it does not fit. */
static int
app_append(
	char *text,
	size_t capacity,
	size_t *length,
	const char *part,
	size_t part_length)
{
	/* A part that does not fit is not added. */
	if (part_length > capacity - *length)
		return E2BIG;

	/* The part at the end. */
	memcpy(text + *length, part, part_length);
	*length += part_length;

	/* Succeeded: the part is added. */
	return 0;
}

/* Writes a new file beside the file, flushes it and renames it over the file; returns 0 or an errno value. */
static int
app_replace(
	const char *path,
	const char *text,
	size_t length)
{
	char temporary[SETTINGS_APP_PATH_MAX + 8U];
	ssize_t count;
	size_t done;
	int descriptor;
	int status;
	int error;

	/* A new file with a name of its own beside the file, the user's alone. */
	(void)snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
	descriptor = mkstemp(temporary);
	if (descriptor < 0)
		return errno;

	/* Every byte of the text. */
	error = 0;
	done = 0;
	while (done < length) {
		count = write(descriptor, text + done, length - done);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			error = errno;
			break;
		}
		done += (size_t)count;
	}

	/* On the disk before it takes the file's name. */
	if (error == 0) {
		status = fsync(descriptor);
		if (status != 0)
			error = errno;
	}

	/* The file is closed either way; a closing that fails is a failed write. */
	status = close(descriptor);
	if (error == 0 && status != 0)
		error = EIO;

	/* A failed write leaves the file as it was. */
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

	/* It replaces the file at once. */
	status = rename(temporary, path);
	if (status != 0) {
		error = errno;
		(void)unlink(temporary);
		return error;
	}

	/* Succeeded: the file holds the new text. */
	return 0;
}
