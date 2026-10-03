/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws135: libkeiland's kl_settings_* for the host tests of its users (Settings' plan/ws089/tests/host-build.sh,
 * Terminal's plan/ws128/tests/terminal-p009.sh), without Wayland: the compositor's keys are held in memory at the
 * table's defaults, a set takes effect at once and is answered at once, an application's keys are its real file
 * (libkeiland's settings-app.c), and the watches run at the next dispatch (libkeiland's settings-cache.c).
 */

#include "settings-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct kl_settings {
	struct settings_cache cache;
	struct settings_app app;
	uint32_t next_request;
};

struct kl_settings *
kl_settings_open(
	struct wl_display *display,
	const char *app)
{
	const struct kl_settings_key *key;
	struct kl_settings *settings;
	char value[16];
	size_t index;

	(void)display;
	settings = calloc(1, sizeof(*settings));
	if (settings == NULL)
		return NULL;
	settings_cache_init(&settings->cache);
	settings->next_request = 1;
	for (index = 0; index < kl_settings_key_count(); index++) {
		key = kl_settings_key_at(index);
		if (key->resolver != KL_SETTINGS_RESOLVER_COMPOSITOR || key->type == KL_SETTINGS_TYPE_PATH)
			continue;
		if ((key->flags & KL_SETTINGS_KEY_PREFIX) != 0U)
			continue;
		snprintf(value, sizeof(value), "%d", key->fallback);
		settings_cache_set(&settings->cache, key->name, value, KL_SETTINGS_DEFAULT, 1U);
	}
	settings_cache_set(&settings->cache, "wallpaper", "", KL_SETTINGS_DEFAULT, 1U);
	if (app != NULL && getenv("HOME") != NULL && settings_app_open(&settings->app, app, getenv("HOME")) == 0)
		settings_app_load(&settings->app, &settings->cache);
	settings_cache_settle(&settings->cache);
	return settings;
}

void
kl_settings_close(
	struct kl_settings *settings)
{
	free(settings);
}

int
kl_settings_get(
	const struct kl_settings *settings,
	const char *key,
	char *value,
	size_t size,
	unsigned *flags)
{
	return settings_cache_get(&settings->cache, key, value, size, flags);
}

int
kl_settings_get_int(
	const struct kl_settings *settings,
	const char *key,
	int fallback)
{
	const struct kl_settings_key *found;
	char value[KL_SETTINGS_VALUE_MAX];
	int number;

	found = kl_settings_key_find(key);
	if (found == NULL || kl_settings_get(settings, key, value, sizeof(value), NULL) != 0)
		return fallback;
	if (kl_settings_key_number(found, value, &number) != 0)
		return fallback;
	return number;
}

int
kl_settings_set(
	struct kl_settings *settings,
	const char *key,
	const char *value,
	uint32_t *request)
{
	const struct kl_settings_key *found;
	int error;

	found = kl_settings_key_find(key);
	if (found == NULL)
		return ENOENT;
	error = kl_settings_key_check(found, value);
	if (error != 0)
		return error;
	if (found->resolver == KL_SETTINGS_RESOLVER_APP) {
		error = settings_app_write(&settings->app, key, value);
		if (error != 0)
			return error;
	}
	settings_cache_set(&settings->cache, key, value, 0U, 1U);
	settings_cache_result(&settings->cache, settings->next_request, 0);
	if (request != NULL)
		*request = settings->next_request;
	settings->next_request++;
	return 0;
}

int
kl_settings_set_int(
	struct kl_settings *settings,
	const char *key,
	int value,
	uint32_t *request)
{
	char text[16];

	snprintf(text, sizeof(text), "%d", value);
	return kl_settings_set(settings, key, text, request);
}

int
kl_settings_reset(
	struct kl_settings *settings,
	const char *key,
	uint32_t *request)
{
	const struct kl_settings_key *found;
	char value[16];

	found = kl_settings_key_find(key);
	if (found == NULL)
		return ENOENT;
	if (found->resolver == KL_SETTINGS_RESOLVER_APP && settings_app_write(&settings->app, key, NULL) != 0)
		return EIO;
	if ((found->flags & KL_SETTINGS_KEY_PREFIX) != 0U) {
		settings_cache_set(&settings->cache, key, "", 0U, 0U);
		settings_cache_result(&settings->cache, settings->next_request, 0);
		settings->next_request++;
		return 0;
	}
	value[0] = '\0';
	if (found->type != KL_SETTINGS_TYPE_PATH)
		snprintf(value, sizeof(value), "%d", found->fallback);
	settings_cache_set(&settings->cache, key, value, KL_SETTINGS_DEFAULT, 1U);
	settings_cache_result(&settings->cache, settings->next_request, 0);
	if (request != NULL)
		*request = settings->next_request;
	settings->next_request++;
	return 0;
}

int
kl_settings_watch(
	struct kl_settings *settings,
	const char *prefix,
	kl_settings_watch_fn fn,
	void *data,
	unsigned *watch)
{
	return settings_cache_watch(&settings->cache, prefix, fn, data, watch);
}

void
kl_settings_unwatch(
	struct kl_settings *settings,
	unsigned watch)
{
	settings_cache_unwatch(&settings->cache, watch);
}

int
kl_settings_dispatch(
	struct kl_settings *settings)
{
	settings_cache_notify(&settings->cache);
	return 0;
}

int
kl_settings_take_result(
	struct kl_settings *settings,
	uint32_t *request,
	int *error)
{
	return settings_cache_take_result(&settings->cache, request, error);
}
