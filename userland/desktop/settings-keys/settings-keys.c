/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The table of the desktop's settings and the checks of their values
 * (settings-keys.h).
 */

#include "userland/desktop/settings-keys/settings-keys.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * Every setting the desktop knows, in the order the compositor reports
 * them.  The compositor's keys come first; the ranges are the ones
 * zdesktop and Settings used before (ws089-p007).
 */
static const struct kl_settings_key settings_keys[] = {
	{ "wallpaper", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_PATH, 0, 0, 0, KL_SETTINGS_KEY_KEPT },
	{ "window.opacity", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_INT, 85, 100, 100, KL_SETTINGS_KEY_KEPT },
	{ "pointer.speed", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_INT, 25, 300, 100, KL_SETTINGS_KEY_KEPT },
	{ "pointer.natural", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_BOOL, 0, 1, 0, KL_SETTINGS_KEY_KEPT },
	{ "keyboard.repeat.rate", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_INT, 5, 60, 25, KL_SETTINGS_KEY_KEPT },
	{ "keyboard.repeat.delay", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_INT, 150, 1000, 400, KL_SETTINGS_KEY_KEPT },
	{ "sound.volume", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_INT, 0, 100, 100, KL_SETTINGS_KEY_KEPT },
	{ "sound.muted", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_BOOL, 0, 1, 0, KL_SETTINGS_KEY_KEPT },
	{ "sound.available", KL_SETTINGS_RESOLVER_COMPOSITOR, KL_SETTINGS_TYPE_BOOL, 0, 1, 0, KL_SETTINGS_KEY_READ_ONLY },
	{ "terminal.ambiguous-wide", KL_SETTINGS_RESOLVER_APP, KL_SETTINGS_TYPE_BOOL, 0, 1, 0, 0U }
};

static int keys_parse(const char *value, long *parsed);

/*
 * Reports how many settings the table has.
 */
size_t
kl_settings_key_count(
	void)
{
	/* The rows of the table. */
	return sizeof(settings_keys) / sizeof(settings_keys[0]);
}

/*
 * Gives the setting at a place in the table; NULL past its end.
 */
const struct kl_settings_key *
kl_settings_key_at(
	size_t index)
{
	size_t count;

	/* A place past the last row names nothing. */
	count = kl_settings_key_count();
	if (index >= count)
		return NULL;

	/* The row. */
	return &settings_keys[index];
}

/*
 * Finds a setting by its name; NULL for a name the table does not have.
 */
const struct kl_settings_key *
kl_settings_key_find(
	const char *name)
{
	size_t count;
	size_t index;
	int differs;

	/* Each row in turn. */
	count = kl_settings_key_count();
	for (index = 0; index < count; index++) {
		differs = strcmp(settings_keys[index].name, name);
		if (differs == 0)
			return &settings_keys[index];
	}

	/* The table has no such setting. */
	return NULL;
}

/*
 * Checks that a value may be given to a setting: well formed, and of its
 * type and within its range.  Returns 0 or EINVAL.
 */
int
kl_settings_key_check(
	const struct kl_settings_key *key,
	const char *value)
{
	long parsed;
	int valid;
	int error;

	/* Any value must fit and hold no control character. */
	valid = kl_settings_value_valid(value);
	if (!valid)
		return EINVAL;

	/* A path is absolute. */
	if (key->type == KL_SETTINGS_TYPE_PATH) {
		if (value[0] != '/')
			return EINVAL;
		return 0;
	}

	/* A number or a Boolean is a whole number within the range, not moved into it. */
	error = keys_parse(value, &parsed);
	if (error != 0)
		return EINVAL;
	if (parsed < (long)key->minimum)
		return EINVAL;
	if (parsed > (long)key->maximum)
		return EINVAL;

	/* Succeeded: the value may be given. */
	return 0;
}

/*
 * Reads a setting's value as a whole number, moved into the setting's
 * range when it lies outside (a value written by hand).  Returns 0, or
 * EINVAL for a value that is not a whole number.
 */
int
kl_settings_key_number(
	const struct kl_settings_key *key,
	const char *value,
	int *number)
{
	long parsed;
	int error;

	/* A whole number, and nothing after it. */
	error = keys_parse(value, &parsed);
	if (error != 0)
		return EINVAL;

	/* Below the range, above it, or within it. */
	if (parsed < (long)key->minimum) {
		*number = key->minimum;
	} else if (parsed > (long)key->maximum) {
		*number = key->maximum;
	} else {
		*number = (int)parsed;
	}

	/* Succeeded: the number within the range. */
	return 0;
}

/*
 * Tells whether a name is well formed: lower-case letters, digits, '.',
 * '_' and '-', not empty and short enough.
 */
int
kl_settings_name_valid(
	const char *name)
{
	size_t length;
	size_t index;
	char character;

	/* An empty name, and one too long, are not. */
	length = strlen(name);
	if (length == 0U)
		return 0;
	if (length >= KL_SETTINGS_KEY_MAX)
		return 0;

	/* Each character must be one of those allowed. */
	for (index = 0; index < length; index++) {
		character = name[index];
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

/*
 * Tells whether a value is well formed: short enough, without a newline
 * or another control character (UTF-8's bytes above 0x7f are allowed).
 */
int
kl_settings_value_valid(
	const char *value)
{
	size_t length;
	size_t index;
	unsigned char character;

	/* A value too long is not. */
	length = strlen(value);
	if (length >= KL_SETTINGS_VALUE_MAX)
		return 0;

	/* No control character. */
	for (index = 0; index < length; index++) {
		character = (unsigned char)value[index];
		if (character < 0x20U || character == 0x7fU)
			return 0;
	}

	/* Every byte is allowed. */
	return 1;
}

/* Reads a whole decimal number with nothing after it; returns 0 or EINVAL. */
static int
keys_parse(
	const char *value,
	long *parsed)
{
	char *end;

	/* The number. */
	errno = 0;
	*parsed = strtol(value, &end, 10);

	/* Nothing read, something after it, or out of a long's range. */
	if (end == value)
		return EINVAL;
	if (*end != '\0')
		return EINVAL;
	if (errno != 0)
		return EINVAL;

	/* Succeeded: a whole number. */
	return 0;
}
