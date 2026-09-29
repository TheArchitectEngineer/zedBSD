/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's preferences (ws089-p007, plan/ws089/proposed/
 * desktop-preferences.md): the user's choices of the desktop's look and
 * input, as key=value lines in ~/.config/keiland/desktop.conf.
 *
 * Settings writes them one key at a time and zdesktop reads them, looking
 * at the file's identity, size and time of change now and then to notice
 * a change.  A write takes an flock of a lock file beside the preferences,
 * reads the file again, changes only its own key (keeping every other
 * line, comments and unknown keys included), writes a new file, flushes it
 * to the disk and renames it over the old one: a reader never sees half a
 * file, and two writers or a hand edit do not lose each other's changes.
 */

#include <keiland.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* The bytes of a path with its NUL. */
#define PREFERENCES_PATH_MAX		1024U

/* How many keys are kept from a reading (the rest of the file is kept on disk but not read). */
#define PREFERENCES_ENTRIES		64U

/* The largest file read or written, in bytes. */
#define PREFERENCES_FILE_MAX		65536U

/* The file's folder under the home, and its name there. */
#define PREFERENCES_FOLDER		".config/keiland"
#define PREFERENCES_NAME		"desktop.conf"

/*
 * One key and its value as the last reading found them.
 */
struct preferences_entry {
	char key[KEILAND_PREFERENCES_KEY_MAX];
	char value[KEILAND_PREFERENCES_VALUE_MAX];
};

/*
 * The preferences of one user, as one program reads and writes them.
 *
 * entries hold the keys of the last reading.  present, inode, size and the
 * time of change are the file's as that reading found it (present 0 for a
 * missing file); a reload compares them with the file's now.  One lives
 * from keiland_preferences_open to keiland_preferences_close.
 */
struct keiland_preferences {
	char folder[PREFERENCES_PATH_MAX];
	char path[PREFERENCES_PATH_MAX];
	struct preferences_entry entries[PREFERENCES_ENTRIES];
	size_t count;
	int present;
	uint64_t inode;
	int64_t size;
	int64_t changed_seconds;
	long changed_nanoseconds;
};

static int preferences_home(char *home, size_t size);
static int preferences_read(struct keiland_preferences *preferences);
static int preferences_load(const char *path, char *text, size_t capacity, size_t *length);
static void preferences_parse(struct keiland_preferences *preferences, const char *text, size_t length);
static void preferences_parse_line(struct keiland_preferences *preferences, const char *line, size_t length);
static int preferences_change(struct keiland_preferences *preferences, const char *key, const char *value);
static int preferences_compose(const char *old_text, size_t old_length, const char *key, const char *value, char *text, size_t capacity, size_t *length);
static int preferences_append(char *text, size_t capacity, size_t *length, const char *part, size_t part_length);
static int preferences_line_is(const char *line, size_t length, const char *key);
static int preferences_replace(const struct keiland_preferences *preferences, const char *text, size_t length);
static int preferences_lock(const struct keiland_preferences *preferences);
static void preferences_mkdir(const char *home);
static int preferences_key_valid(const char *key);
static int preferences_value_valid(const char *value);

/*
 * Opens the user's preferences and reads them (a missing file has none).
 *
 * Returns NULL with errno set: ENOENT (no home), ENAMETOOLONG, ENOMEM.
 */
struct keiland_preferences *
keiland_preferences_open(
	void)
{
	struct keiland_preferences *preferences;
	char home[PREFERENCES_PATH_MAX];
	int written;
	int error;

	/* The user's home. */
	error = preferences_home(home, sizeof(home));
	if (error != 0) {
		errno = error;
		return NULL;
	}

	/* The record, empty. */
	preferences = calloc(1, sizeof(*preferences));
	if (preferences == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* The folder and the file in it. */
	written = snprintf(preferences->folder, sizeof(preferences->folder), "%s/%s", home, PREFERENCES_FOLDER);
	if (written < 0 || (size_t)written >= sizeof(preferences->folder)) {
		free(preferences);
		errno = ENAMETOOLONG;
		return NULL;
	}

	/* The file in the folder. */
	written = snprintf(preferences->path, sizeof(preferences->path), "%s/%s", preferences->folder, PREFERENCES_NAME);
	if (written < 0 || (size_t)written >= sizeof(preferences->path)) {
		free(preferences);
		errno = ENAMETOOLONG;
		return NULL;
	}

	/*
	 * What the file holds now.  A file that cannot be read counts as
	 * empty; the next reload tries it again.
	 */
	(void)preferences_read(preferences);

	/* Succeeded: the preferences are open. */
	return preferences;
}

/*
 * Closes the preferences.
 */
void
keiland_preferences_close(
	struct keiland_preferences *preferences)
{
	/* The record is all there is. */
	free(preferences);
}

/*
 * Reads the preferences again when the file's identity, size or time of
 * change moved since the last reading.
 *
 * Returns 0 with *changed 1 when they were read again (else 0), or an
 * errno value of reading.
 */
int
keiland_preferences_reload(
	struct keiland_preferences *preferences,
	int *changed)
{
	struct stat status;
	int result;
	int error;

	/* Nothing changed until the file says so. */
	*changed = 0;

	/* The file now. */
	result = stat(preferences->path, &status);
	if (result != 0) {
		/* Still missing: nothing to read. */
		if (errno == ENOENT && preferences->present == 0)
			return 0;

		/* Gone since the last reading: it now holds nothing. */
		if (errno == ENOENT) {
			preferences->present = 0;
			preferences->count = 0;
			*changed = 1;
			return 0;
		}

		/* The file cannot be looked at. */
		return errno;
	}

	/* The same file, the same size, the same time of change: the same preferences. */
	if (preferences->present != 0 &&
	    preferences->inode == (uint64_t)status.st_ino &&
	    preferences->size == (int64_t)status.st_size &&
	    preferences->changed_seconds == (int64_t)status.st_mtim.tv_sec &&
	    preferences->changed_nanoseconds == status.st_mtim.tv_nsec)
		return 0;

	/* Something moved: they are read again. */
	error = preferences_read(preferences);
	if (error != 0)
		return error;

	/* Succeeded: the new preferences are read. */
	*changed = 1;
	return 0;
}

/*
 * Copies a key's value.  Returns 0, ENOENT (the key is not set) or ERANGE
 * (the value does not fit).
 */
int
keiland_preferences_get(
	const struct keiland_preferences *preferences,
	const char *key,
	char *value,
	size_t size)
{
	size_t index;
	size_t length;
	int differs;

	/* The key among those read. */
	for (index = 0; index < preferences->count; index++) {
		differs = strcmp(preferences->entries[index].key, key);
		if (differs != 0)
			continue;

		/* A value that does not fit is not cut. */
		length = strlen(preferences->entries[index].value);
		if (length + 1U > size)
			return ERANGE;

		/* Succeeded: the value is copied. */
		memcpy(value, preferences->entries[index].value, length + 1U);
		return 0;
	}

	/* The key is not set. */
	return ENOENT;
}

/*
 * Reports a key's value as a whole number within minimum..maximum (a value
 * outside is moved to the nearer end); fallback when the key is not set or
 * is not a number.
 */
int
keiland_preferences_get_int(
	const struct keiland_preferences *preferences,
	const char *key,
	int fallback,
	int minimum,
	int maximum)
{
	char value[KEILAND_PREFERENCES_VALUE_MAX];
	char *end;
	long number;
	int error;

	/* The value as text. */
	error = keiland_preferences_get(preferences, key, value, sizeof(value));
	if (error != 0)
		return fallback;

	/* A whole number, and nothing after it. */
	errno = 0;
	number = strtol(value, &end, 10);
	if (end == value ||
	    *end != '\0' ||
	    errno != 0)
		return fallback;

	/* Below the range. */
	if (number < (long)minimum)
		return minimum;

	/* Above the range. */
	if (number > (long)maximum)
		return maximum;

	/* Within it. */
	return (int)number;
}

/*
 * Sets a key's value in the file (and in what was read).  Returns 0, or
 * EINVAL (a key or a value that is not allowed), E2BIG (a file too large),
 * or an errno value of writing.
 */
int
keiland_preferences_set(
	struct keiland_preferences *preferences,
	const char *key,
	const char *value)
{
	int valid;
	int error;

	/* Only a well-formed value is written (the key is checked with the change). */
	valid = preferences_value_valid(value);
	if (valid == 0)
		return EINVAL;

	/* The file with the key set. */
	error = preferences_change(preferences, key, value);
	if (error != 0)
		return error;

	/* Succeeded: the key is set. */
	return 0;
}

/*
 * Removes a key from the file (and from what was read); a key not set is
 * no error.  Returns 0, EINVAL, E2BIG or an errno value of writing.
 */
int
keiland_preferences_unset(
	struct keiland_preferences *preferences,
	const char *key)
{
	int error;

	/* The file without the key. */
	error = preferences_change(preferences, key, NULL);
	if (error != 0)
		return error;

	/* Succeeded: the key is not set. */
	return 0;
}

/* Finds the user's home: $HOME, else the password file's; returns 0 or ENOENT. */
static int
preferences_home(
	char *home,
	size_t size)
{
	const struct passwd *user;
	const char *given;
	int written;

	/* $HOME when it is an absolute path. */
	given = getenv("HOME");
	if (given == NULL || given[0] != '/') {
		/* Else the home the password file gives the user. */
		user = getpwuid(getuid());
		if (user == NULL ||
		    user->pw_dir == NULL ||
		    user->pw_dir[0] != '/')
			return ENOENT;
		given = user->pw_dir;
	}

	/* The home must fit. */
	written = snprintf(home, size, "%s", given);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the home is known. */
	return 0;
}

/* Reads the file into the entries and records its identity, size and time of change; returns 0 or an errno value. */
static int
preferences_read(
	struct keiland_preferences *preferences)
{
	struct stat status;
	char *text;
	size_t length;
	int result;
	int error;

	/* Room for the file's text. */
	text = malloc(PREFERENCES_FILE_MAX);
	if (text == NULL)
		return ENOMEM;

	/* The file's identity first, so that a change while it is read shows at the next reload. */
	result = stat(preferences->path, &status);
	if (result != 0) {
		/* A missing file holds nothing. */
		error = errno;
		free(text);
		preferences->count = 0;
		preferences->present = 0;
		if (error == ENOENT)
			return 0;
		return error;
	}

	/* The text. */
	error = preferences_load(preferences->path, text, PREFERENCES_FILE_MAX, &length);
	if (error != 0) {
		free(text);
		return error;
	}

	/* Its keys, and the file it was. */
	preferences_parse(preferences, text, length);
	free(text);
	preferences->present = 1;
	preferences->inode = (uint64_t)status.st_ino;
	preferences->size = (int64_t)status.st_size;
	preferences->changed_seconds = (int64_t)status.st_mtim.tv_sec;
	preferences->changed_nanoseconds = status.st_mtim.tv_nsec;

	/* Succeeded: the preferences are read. */
	return 0;
}

/* Reads a file's whole text (a missing file is empty); returns 0, E2BIG for a file too large, or an errno value. */
static int
preferences_load(
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
	descriptor = open(path, O_RDONLY);
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
			error = errno;
			break;
		}

		/* Nothing more is the end; else the bytes join the text. */
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

/* Takes the keys out of a file's text, as many as are kept. */
static void
preferences_parse(
	struct keiland_preferences *preferences,
	const char *text,
	size_t length)
{
	size_t start;
	size_t end;

	/* No key yet. */
	preferences->count = 0;

	/* Each line, the last one with or without its newline. */
	start = 0;
	while (start < length) {
		/* The line's end. */
		end = start;
		while (end < length && text[end] != '\n')
			end++;

		/* Its key, if it is a line of one. */
		preferences_parse_line(preferences, text + start, end - start);
		start = end + 1U;
	}
}

/* Keeps a key=value line's key and value; a comment, an empty line or a line that is not well formed is passed over. */
static void
preferences_parse_line(
	struct keiland_preferences *preferences,
	const char *line,
	size_t length)
{
	struct preferences_entry *entry;
	const char *equals;
	size_t key_length;
	size_t value_length;
	size_t index;
	int valid;
	int differs;

	/* An empty line and a comment hold no key. */
	if (length == 0U || line[0] == '#')
		return;

	/* The key before the first '='. */
	equals = memchr(line, '=', length);
	if (equals == NULL)
		return;
	key_length = (size_t)(equals - line);
	value_length = length - key_length - 1U;

	/* Both must fit. */
	if (key_length == 0U || key_length >= KEILAND_PREFERENCES_KEY_MAX)
		return;
	if (value_length >= KEILAND_PREFERENCES_VALUE_MAX)
		return;

	/* A full table keeps the keys it has. */
	if (preferences->count == PREFERENCES_ENTRIES)
		return;

	/* The key and the value, as strings. */
	entry = &preferences->entries[preferences->count];
	memcpy(entry->key, line, key_length);
	entry->key[key_length] = '\0';
	memcpy(entry->value, equals + 1, value_length);
	entry->value[value_length] = '\0';

	/* A key that is not well formed is not kept. */
	valid = preferences_key_valid(entry->key);
	if (valid == 0)
		return;

	/* A key given twice keeps its first value (the writer keeps only one line of a key). */
	for (index = 0; index < preferences->count; index++) {
		differs = strcmp(preferences->entries[index].key, entry->key);
		if (differs == 0)
			return;
	}

	/* The entry is kept. */
	preferences->count++;
}

/* Sets (value not NULL) or removes a key in the file under the lock, then reads the file again; returns 0 or an errno value. */
static int
preferences_change(
	struct keiland_preferences *preferences,
	const char *key,
	const char *value)
{
	char *old_text;
	char *text;
	size_t old_length;
	size_t length;
	int valid;
	int lock;
	int error;

	/* Only a well-formed key is written. */
	valid = preferences_key_valid(key);
	if (valid == 0)
		return EINVAL;

	/* Room for the file as it is, and as it is to be. */
	old_text = malloc(PREFERENCES_FILE_MAX);
	if (old_text == NULL)
		return ENOMEM;
	text = malloc(PREFERENCES_FILE_MAX);
	if (text == NULL) {
		free(old_text);
		return ENOMEM;
	}

	/* The folder, then the lock beside the file (held until closed). */
	preferences_mkdir(preferences->folder);
	lock = preferences_lock(preferences);
	if (lock < 0) {
		error = errno;
		free(text);
		free(old_text);
		return error;
	}

	/* The file as it is now, under the lock, with only this key changed, written in place of the old. */
	error = preferences_load(preferences->path, old_text, PREFERENCES_FILE_MAX, &old_length);
	if (error == 0)
		error = preferences_compose(old_text, old_length, key, value, text, PREFERENCES_FILE_MAX, &length);
	if (error == 0)
		error = preferences_replace(preferences, text, length);

	/* The lock and the room go. */
	(void)close(lock);
	free(text);
	free(old_text);

	/* Reports why the file could not be changed. */
	if (error != 0)
		return error;

	/* What was written is what is read now. */
	error = preferences_read(preferences);
	if (error != 0)
		return error;

	/* Succeeded: the file holds the change. */
	return 0;
}

/* Writes a file's text with one key set or removed: the key's first line replaced, its other lines dropped, every other line kept; returns 0 or E2BIG. */
static int
preferences_compose(
	const char *old_text,
	size_t old_length,
	const char *key,
	const char *value,
	char *text,
	size_t capacity,
	size_t *length)
{
	size_t start;
	size_t end;
	int written;
	int matched;
	int error;

	/* Nothing written yet, and the key not yet met. */
	*length = 0;
	written = 0;
	error = 0;

	/* Each line of the old text. */
	start = 0;
	while (error == 0 && start < old_length) {
		/* The line's end. */
		end = start;
		while (end < old_length && old_text[end] != '\n')
			end++;

		/* The key's line: its first becomes the new value (or goes), the others go. */
		matched = preferences_line_is(old_text + start, end - start, key);
		if (matched != 0) {
			if (written == 0 && value != NULL) {
				error = preferences_append(text, capacity, length, key, strlen(key));
				if (error == 0)
					error = preferences_append(text, capacity, length, "=", 1U);
				if (error == 0)
					error = preferences_append(text, capacity, length, value, strlen(value));
				if (error == 0)
					error = preferences_append(text, capacity, length, "\n", 1U);
			}

			/* The key has had its line. */
			written = 1;
		} else {
			/* Any other line stays as it was. */
			error = preferences_append(text, capacity, length, old_text + start, end - start);
			if (error == 0)
				error = preferences_append(text, capacity, length, "\n", 1U);
		}

		/* The next line. */
		start = end + 1U;
	}

	/* A key the file did not have goes at its end. */
	if (error == 0 &&
	    written == 0 &&
	    value != NULL) {
		error = preferences_append(text, capacity, length, key, strlen(key));
		if (error == 0)
			error = preferences_append(text, capacity, length, "=", 1U);
		if (error == 0)
			error = preferences_append(text, capacity, length, value, strlen(value));
		if (error == 0)
			error = preferences_append(text, capacity, length, "\n", 1U);
	}

	/* Reports a text too large. */
	if (error != 0)
		return error;

	/* Succeeded: the new text is written. */
	return 0;
}

/* Adds a part to a text; returns 0, or E2BIG when it does not fit. */
static int
preferences_append(
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

/* Tells whether a line is the key's (starts with the key and '='). */
static int
preferences_line_is(
	const char *line,
	size_t length,
	const char *key)
{
	size_t key_length;
	int differs;

	/* The line must hold the key and the '='. */
	key_length = strlen(key);
	if (length <= key_length)
		return 0;

	/* The key's characters, then the '='. */
	differs = memcmp(line, key, key_length);
	if (differs != 0)
		return 0;
	if (line[key_length] != '=')
		return 0;

	/* It is the key's line. */
	return 1;
}

/* Writes a new file beside the preferences, flushes it and renames it over them; returns 0 or an errno value. */
static int
preferences_replace(
	const struct keiland_preferences *preferences,
	const char *text,
	size_t length)
{
	char temporary[PREFERENCES_PATH_MAX + 8U];
	ssize_t count;
	size_t done;
	int descriptor;
	int status;
	int error;

	/* A new file with a name of its own beside the preferences. */
	(void)snprintf(temporary, sizeof(temporary), "%s.XXXXXX", preferences->path);
	descriptor = mkstemp(temporary);
	if (descriptor < 0)
		return errno;

	/* Every byte of the text. */
	error = 0;
	done = 0;
	while (done < length) {
		count = write(descriptor, text + done, length - done);
		if (count < 0) {
			error = errno;
			break;
		}

		/* The bytes written. */
		done += (size_t)count;
	}

	/* On the disk before it takes the preferences' name. */
	if (error == 0) {
		status = fsync(descriptor);
		if (status != 0)
			error = errno;
	}

	/* The file is closed either way; a closing that fails is a failed write. */
	status = close(descriptor);
	if (error == 0 && status != 0)
		error = EIO;

	/* A failed write leaves the preferences as they were. */
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

	/* It replaces the preferences at once. */
	status = rename(temporary, preferences->path);
	if (status != 0) {
		error = errno;
		(void)unlink(temporary);
		return error;
	}

	/* Succeeded: the preferences hold the new text. */
	return 0;
}

/* Takes the lock beside the preferences; returns its descriptor, or -1 with errno set. */
static int
preferences_lock(
	const struct keiland_preferences *preferences)
{
	char path[PREFERENCES_PATH_MAX + 8U];
	int descriptor;
	int status;
	int error;

	/* The lock file. */
	(void)snprintf(path, sizeof(path), "%s.lock", preferences->path);
	descriptor = open(path, O_RDWR | O_CREAT, 0600);
	if (descriptor < 0)
		return -1;

	/* Held until the descriptor is closed. */
	status = flock(descriptor, LOCK_EX);
	if (status != 0) {
		error = errno;
		(void)close(descriptor);
		errno = error;
		return -1;
	}

	/* Succeeded: the lock is held. */
	return descriptor;
}

/* Makes the preferences' folder and the folders above it that are missing (as mkdir -p), for the user alone. */
static void
preferences_mkdir(
	const char *folder)
{
	char partial[PREFERENCES_PATH_MAX];
	size_t index;

	/* Each prefix that ends at a slash, then the whole path. */
	(void)snprintf(partial, sizeof(partial), "%s", folder);
	for (index = 1; partial[index] != '\0'; index++) {
		if (partial[index] != '/')
			continue;

		/* The folder up to this slash; one that exists is left alone. */
		partial[index] = '\0';
		(void)mkdir(partial, 0700);
		partial[index] = '/';
	}

	/* The folder itself. */
	(void)mkdir(partial, 0700);
}

/* Tells whether a key is well formed: lower-case letters, digits, '.', '_' and '-', and short enough. */
static int
preferences_key_valid(
	const char *key)
{
	size_t index;
	size_t length;
	char character;

	/* An empty key, and one too long, are not. */
	length = strlen(key);
	if (length == 0U)
		return 0;
	if (length >= KEILAND_PREFERENCES_KEY_MAX)
		return 0;

	/* Each character must be one of those allowed. */
	for (index = 0; key[index] != '\0'; index++) {
		character = key[index];
		if (character >= 'a' && character <= 'z')
			continue;
		if (character >= '0' && character <= '9')
			continue;
		if (character == '.' ||
		    character == '_' ||
		    character == '-')
			continue;
		return 0;
	}

	/* Every character is allowed. */
	return 1;
}

/* Tells whether a value is well formed: short enough, without a newline or another control character. */
static int
preferences_value_valid(
	const char *value)
{
	size_t index;
	size_t length;
	unsigned char character;

	/* A value too long is not. */
	length = strlen(value);
	if (length >= KEILAND_PREFERENCES_VALUE_MAX)
		return 0;

	/* No control character (UTF-8's bytes above 0x7f are allowed). */
	for (index = 0; value[index] != '\0'; index++) {
		character = (unsigned char)value[index];
		if (character < 0x20U || character == 0x7fU)
			return 0;
	}

	/* Every byte is allowed. */
	return 1;
}
