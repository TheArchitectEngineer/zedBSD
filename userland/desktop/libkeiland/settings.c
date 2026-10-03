/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's settings for applications (keiland.h's kl_settings_*;
 * WS135, plan/ws135/design.md section 3): the client of Keiland's system
 * extension (kl_system_manager_v1, kl_system_settings_v1;
 * keiland/kl-system-protocol.h) for the compositor's keys, and the
 * application's own file (settings-app.c) for its keys.
 *
 * The extension's objects live on a queue of the library's own, which
 * stays theirs: an event already queued keeps the queue it was read for
 * (libwayland's wl_proxy_set_queue), so moving the objects to the
 * application's queue could lose a change sent right after the first
 * state.  The application's loop reads the display; kl_settings_dispatch
 * dispatches this queue and then runs the watches, never within the
 * display's dispatch.
 *
 * The protocol's interfaces are described here, as wayland-scanner would
 * make them, over libwayland's marshalling.
 */

#include <keiland.h>

#include <wayland-client.h>

#include "settings-private.h"
#include "userland/desktop/keiland/kl-system-protocol.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Marks protocol callback arguments that this client does not inspect. */
#define UNUSED_PARAMETER(name) ((void)(name))

/*
 * One application's settings: the display, the library's queue with the
 * manager and the settings object on it (NULL without the extension, or
 * once the compositor went), the cache, the application's file, and the
 * number of the next request.
 */
struct kl_settings {
	struct wl_display *display;
	struct wl_event_queue *queue;
	struct wl_proxy *manager;
	struct wl_proxy *proxy;
	struct settings_cache cache;
	struct settings_app app;
	uint32_t next_request;
	unsigned lost;
};

/* What the registry search found: the manager's global name, 0 for none. */
struct settings_search {
	uint32_t name;
};

/* The listener of kl_system_manager_v1's event, as libwayland calls it. */
struct settings_manager_listener {
	void (*capabilities)(void *data, struct wl_proxy *proxy, uint32_t bits);
};

/* The listener of kl_system_settings_v1's events, as libwayland calls it. */
struct settings_proxy_listener {
	void (*value)(void *data, struct wl_proxy *proxy, const char *key, const char *value, uint32_t flags);
	void (*done)(void *data, struct wl_proxy *proxy, uint32_t serial);
	void (*result)(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);
};

static void settings_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void settings_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void settings_capabilities(void *data, struct wl_proxy *proxy, uint32_t bits);
static void settings_value(void *data, struct wl_proxy *proxy, const char *key, const char *value, uint32_t flags);
static void settings_done(void *data, struct wl_proxy *proxy, uint32_t serial);
static void settings_result(void *data, struct wl_proxy *proxy, uint32_t request, uint32_t applied, uint32_t saved);

extern const struct wl_interface kl_system_manager_v1_interface;
extern const struct wl_interface kl_system_settings_v1_interface;

/* get_settings: the new settings object. */
static const struct wl_interface *settings_get_types[] = {
	&kl_system_settings_v1_interface,
};

/* The arguments of messages that name no interface (at most three). */
static const struct wl_interface *settings_plain_types[] = {
	NULL,
	NULL,
	NULL,
};

/* The requests of kl_system_manager_v1. */
static const struct wl_message settings_manager_requests[] = {
	{ "destroy", "", NULL },
	{ "get_settings", "n", settings_get_types },
};

/* The events of kl_system_manager_v1. */
static const struct wl_message settings_manager_events[] = {
	{ "capabilities", "u", settings_plain_types },
};

/* kl_system_manager_v1. */
const struct wl_interface kl_system_manager_v1_interface = {
	KL_SYSTEM_MANAGER_NAME,
	1,
	2,
	settings_manager_requests,
	1,
	settings_manager_events
};

/* The requests of kl_system_settings_v1. */
static const struct wl_message settings_requests[] = {
	{ "destroy", "", NULL },
	{ "set", "uss", settings_plain_types },
	{ "reset", "us", settings_plain_types },
};

/* The events of kl_system_settings_v1. */
static const struct wl_message settings_events[] = {
	{ "value", "ssu", settings_plain_types },
	{ "done", "u", settings_plain_types },
	{ "result", "uuu", settings_plain_types },
};

/* kl_system_settings_v1. */
const struct wl_interface kl_system_settings_v1_interface = {
	KL_SYSTEM_SETTINGS_NAME,
	1,
	3,
	settings_requests,
	3,
	settings_events
};

/* The registry's callbacks while the manager is looked for. */
static const struct wl_registry_listener settings_registry_listener = {
	settings_global,
	settings_global_remove
};

/* The manager's listener. */
static const struct settings_manager_listener settings_manager_listener = {
	settings_capabilities
};

/* The settings object's listener. */
static const struct settings_proxy_listener settings_proxy_listener = {
	settings_value,
	settings_done,
	settings_result
};

static int settings_home(char *home, size_t size);
static void settings_bind(struct kl_settings *settings);
static int settings_error_of(uint32_t applied);
static int settings_app_change(struct kl_settings *settings, const char *key, const char *value, uint32_t *request);

/*
 * Opens the settings on a display: the application's own keys from its
 * file, and the compositor's from its extension, waiting once for them.
 */
struct kl_settings *
kl_settings_open(
	struct wl_display *display,
	const char *app)
{
	struct kl_settings *settings;
	char home[SETTINGS_APP_PATH_MAX];
	int error;

	/* The record, with no value yet. */
	settings = calloc(1, sizeof(*settings));
	if (settings == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	settings->display = display;
	settings->next_request = 1;
	settings_cache_init(&settings->cache);

	/* The application's own keys, at their defaults and then the file's values. */
	if (app != NULL) {
		error = settings_home(home, sizeof(home));
		if (error == 0)
			error = settings_app_open(&settings->app, app, home);
		if (error == 0)
			settings_app_load(&settings->app, &settings->cache);
	}

	/* The compositor's keys, through its extension when it has one. */
	if (display != NULL)
		settings_bind(settings);

	/* Succeeded: what the settings started with is not told as a change. */
	settings_cache_settle(&settings->cache);
	return settings;
}

/*
 * Closes the settings.
 */
void
kl_settings_close(
	struct kl_settings *settings)
{
	/* Nothing to close. */
	if (settings == NULL)
		return;

	/* The settings object, then the manager, then the queue. */
	if (settings->proxy != NULL) {
		wl_proxy_marshal(settings->proxy, KL_SYSTEM_SETTINGS_DESTROY);
		wl_proxy_destroy(settings->proxy);
	}
	if (settings->manager != NULL) {
		wl_proxy_marshal(settings->manager, KL_SYSTEM_MANAGER_DESTROY);
		wl_proxy_destroy(settings->manager);
	}
	if (settings->queue != NULL)
		wl_event_queue_destroy(settings->queue);

	/* The record. */
	free(settings);
}

/*
 * Copies a key's value and flags.
 */
int
kl_settings_get(
	const struct kl_settings *settings,
	const char *key,
	char *value,
	size_t size,
	unsigned *flags)
{
	const struct kl_settings_key *found;
	int error;

	/* A key of the desktop's. */
	found = kl_settings_key_find(key);
	if (found == NULL)
		return ENOENT;

	/* A compositor's key needs the compositor's extension. */
	if (found->resolver == KL_SETTINGS_RESOLVER_COMPOSITOR && settings->proxy == NULL)
		return ENOTSUP;

	/* An application's key of another application is not this one's. */
	if (found->resolver == KL_SETTINGS_RESOLVER_APP && !settings->app.used)
		return ENOTSUP;

	/* The cache's value. */
	error = settings_cache_get(&settings->cache, key, value, size, flags);
	if (error != 0)
		return error;

	/* Succeeded: the value. */
	return 0;
}

/*
 * Reports a key's value as a whole number, or fallback.
 */
int
kl_settings_get_int(
	const struct kl_settings *settings,
	const char *key,
	int fallback)
{
	const struct kl_settings_key *found;
	char value[KL_SETTINGS_VALUE_MAX];
	int number;
	int error;

	/* The value as text. */
	error = kl_settings_get(settings, key, value, sizeof(value), NULL);
	if (error != 0)
		return fallback;

	/* A whole number within the key's range. */
	found = kl_settings_key_find(key);
	error = kl_settings_key_number(found, value, &number);
	if (error != 0)
		return fallback;

	/* Succeeded: the number. */
	return number;
}

/*
 * Asks for a key to take a value.
 */
int
kl_settings_set(
	struct kl_settings *settings,
	const char *key,
	const char *value,
	uint32_t *request)
{
	const struct kl_settings_key *found;
	uint32_t number;
	int error;

	/* A key of the desktop's that may be set. */
	found = kl_settings_key_find(key);
	if (found == NULL)
		return ENOENT;
	if ((found->flags & KL_SETTINGS_KEY_READ_ONLY) != 0U)
		return EPERM;

	/* A value of its type and within its range. */
	error = kl_settings_key_check(found, value);
	if (error != 0)
		return error;

	/* An application's key: its file, now. */
	if (found->resolver == KL_SETTINGS_RESOLVER_APP) {
		error = settings_app_change(settings, key, value, request);
		if (error != 0)
			return error;
		return 0;
	}

	/* A compositor's key needs the compositor's extension. */
	if (settings->proxy == NULL)
		return ENOTSUP;

	/* The request, sent with the application's next flush. */
	number = settings->next_request;
	settings->next_request++;
	wl_proxy_marshal(settings->proxy, KL_SYSTEM_SETTINGS_SET, number, key, value);

	/* Succeeded: the answer comes as a result, the value as a change. */
	if (request != NULL)
		*request = number;
	return 0;
}

/*
 * Asks for a key to take a whole number.
 */
int
kl_settings_set_int(
	struct kl_settings *settings,
	const char *key,
	int value,
	uint32_t *request)
{
	char text[16];
	int error;

	/* The number as text. */
	(void)snprintf(text, sizeof(text), "%d", value);
	error = kl_settings_set(settings, key, text, request);
	if (error != 0)
		return error;

	/* Succeeded: asked. */
	return 0;
}

/*
 * Asks for a key to go back to its default.
 */
int
kl_settings_reset(
	struct kl_settings *settings,
	const char *key,
	uint32_t *request)
{
	const struct kl_settings_key *found;
	uint32_t number;
	int error;

	/* A key of the desktop's that may be set. */
	found = kl_settings_key_find(key);
	if (found == NULL)
		return ENOENT;
	if ((found->flags & KL_SETTINGS_KEY_READ_ONLY) != 0U)
		return EPERM;

	/* An application's key: its line leaves the file. */
	if (found->resolver == KL_SETTINGS_RESOLVER_APP) {
		error = settings_app_change(settings, key, NULL, request);
		if (error != 0)
			return error;
		return 0;
	}

	/* A compositor's key needs the compositor's extension. */
	if (settings->proxy == NULL)
		return ENOTSUP;

	/* The request. */
	number = settings->next_request;
	settings->next_request++;
	wl_proxy_marshal(settings->proxy, KL_SYSTEM_SETTINGS_RESET, number, key);

	/* Succeeded: asked. */
	if (request != NULL)
		*request = number;
	return 0;
}

/*
 * Watches the keys that start with prefix.
 */
int
kl_settings_watch(
	struct kl_settings *settings,
	const char *prefix,
	kl_settings_watch_fn fn,
	void *data,
	unsigned *watch)
{
	int error;

	/* A watch in the cache. */
	error = settings_cache_watch(&settings->cache, prefix, fn, data, watch);
	if (error != 0)
		return error;

	/* Succeeded: the watch hears the next change. */
	return 0;
}

/*
 * Stops a watch.
 */
void
kl_settings_unwatch(
	struct kl_settings *settings,
	unsigned watch)
{
	/* The cache's watch. */
	settings_cache_unwatch(&settings->cache, watch);
}

/*
 * Takes the compositor's events the display has read, then runs the
 * watches of the keys that changed.
 */
int
kl_settings_dispatch(
	struct kl_settings *settings)
{
	int status;
	int error;

	/* The compositor's events on the library's queue (they only fill the cache). */
	if (settings->queue != NULL && !settings->lost) {
		status = wl_display_dispatch_queue_pending(settings->display, settings->queue);
		error = wl_display_get_error(settings->display);
		if (status < 0 || error != 0) {
			/* The compositor went: its keys have no value from now on. */
			settings->lost = 1;
			settings_cache_lost(&settings->cache, KL_SETTINGS_RESOLVER_COMPOSITOR);
		}
	}

	/* The watches of what changed. */
	settings_cache_notify(&settings->cache);

	/* The compositor went. */
	if (settings->lost)
		return EPIPE;

	/* Succeeded: every change was told. */
	return 0;
}

/*
 * Takes one finished request.
 */
int
kl_settings_take_result(
	struct kl_settings *settings,
	uint32_t *request,
	int *error)
{
	int taken;

	/* The oldest finished request, if any. */
	taken = settings_cache_take_result(&settings->cache, request, error);

	/* 1 with one, 0 without. */
	return taken;
}

/* Finds the user's home: $HOME, else the password file's; returns 0 or ENOENT. */
static int
settings_home(
	char *home,
	size_t size)
{
	const struct passwd *user;
	const char *given;
	int written;

	/* $HOME when it is an absolute path, else the password file's. */
	given = getenv("HOME");
	if (given == NULL || given[0] != '/') {
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

/*
 * Binds the compositor's system manager on the library's queue, makes the
 * settings object and waits once for its first state.  Without the
 * extension the compositor's keys stay without a value (ENOTSUP).
 */
static void
settings_bind(
	struct kl_settings *settings)
{
	struct settings_search search;
	struct wl_display *wrapper;
	struct wl_registry *registry;
	int status;

	/* The library's queue. */
	settings->queue = wl_display_create_queue(settings->display);
	if (settings->queue == NULL)
		return;

	/* The display as the search sees it: what it makes lives on the queue. */
	wrapper = wl_proxy_create_wrapper(settings->display);
	if (wrapper == NULL)
		return;
	wl_proxy_set_queue((struct wl_proxy *)wrapper, settings->queue);

	/* The globals, announced to this search alone. */
	search.name = 0;
	registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	if (registry == NULL)
		return;
	status = wl_registry_add_listener(registry, &settings_registry_listener, &search);
	if (status == 0)
		status = wl_display_roundtrip_queue(settings->display, settings->queue);
	if (status < 0 || search.name == 0U) {
		wl_registry_destroy(registry);
		return;
	}

	/* The manager, on the queue as the registry is. */
	settings->manager = wl_registry_bind(registry, search.name, &kl_system_manager_v1_interface, KL_SYSTEM_MANAGER_VERSION);
	wl_registry_destroy(registry);
	if (settings->manager == NULL)
		return;
	(void)wl_proxy_add_listener(settings->manager, (void (**)(void))&settings_manager_listener, settings);

	/* The settings object, on the same queue. */
	settings->proxy = wl_proxy_marshal_constructor(settings->manager, KL_SYSTEM_MANAGER_GET_SETTINGS, &kl_system_settings_v1_interface, NULL);
	if (settings->proxy == NULL)
		return;
	(void)wl_proxy_add_listener(settings->proxy, (void (**)(void))&settings_proxy_listener, settings);

	/* The first state: every compositor key and a done. */
	status = wl_display_roundtrip_queue(settings->display, settings->queue);
	if (status < 0) {
		settings->lost = 1;
		settings_cache_lost(&settings->cache, KL_SETTINGS_RESOLVER_COMPOSITOR);
	}
}

/* Notes the system manager's global. */
static void
settings_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct settings_search *search;
	int differs;

	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(version);

	/* Only the system manager. */
	search = data;
	differs = strcmp(interface, KL_SYSTEM_MANAGER_NAME);
	if (differs == 0)
		search->name = name;
}

/* A global that goes is not the search's concern. */
static void
settings_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(name);
}

/* Takes the manager's capabilities (version 1 offers only the settings). */
static void
settings_capabilities(
	void *data,
	struct wl_proxy *proxy,
	uint32_t bits)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(bits);
}

/* Notes a value the compositor sent, in effect at its done. */
static void
settings_value(
	void *data,
	struct wl_proxy *proxy,
	const char *key,
	const char *value,
	uint32_t flags)
{
	struct kl_settings *settings;
	unsigned present;
	unsigned shown;

	UNUSED_PARAMETER(proxy);

	/* A value not known yet has none; the default flag is the application's. */
	settings = data;
	present = 1;
	if ((flags & KL_SYSTEM_SETTINGS_UNKNOWN) != 0U)
		present = 0;
	shown = 0;
	if ((flags & KL_SYSTEM_SETTINGS_DEFAULT) != 0U)
		shown = KL_SETTINGS_DEFAULT;
	settings_cache_pending(&settings->cache, key, value, shown, present);
}

/* Puts the compositor's values into effect. */
static void
settings_done(
	void *data,
	struct wl_proxy *proxy,
	uint32_t serial)
{
	struct kl_settings *settings;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(serial);

	/* The pending values are one state. */
	settings = data;
	settings_cache_done(&settings->cache);
}

/* Keeps a finished request with its error. */
static void
settings_result(
	void *data,
	struct wl_proxy *proxy,
	uint32_t request,
	uint32_t applied,
	uint32_t saved)
{
	struct kl_settings *settings;
	int error;

	UNUSED_PARAMETER(proxy);
	UNUSED_PARAMETER(saved);

	/* The result for kl_settings_take_result. */
	settings = data;
	error = settings_error_of(applied);
	settings_cache_result(&settings->cache, request, error);
}

/* Gives the errno value a request's result means. */
static int
settings_error_of(
	uint32_t applied)
{
	/* Each result the compositor gives. */
	switch (applied) {
	case KL_SYSTEM_RESULT_OK:
		return 0;
	case KL_SYSTEM_RESULT_DENIED:
		return EPERM;
	case KL_SYSTEM_RESULT_UNSUPPORTED:
		return ENOTSUP;
	case KL_SYSTEM_RESULT_BUSY:
		return EBUSY;
	case KL_SYSTEM_RESULT_INVALID:
		return EINVAL;
	case KL_SYSTEM_RESULT_UNAVAILABLE:
		return ENODEV;
	default:
		break;
	}

	/* Anything else failed. */
	return EIO;
}

/* Writes an application's key into its file and puts it into effect at once, with its result finished. */
static int
settings_app_change(
	struct kl_settings *settings,
	const char *key,
	const char *value,
	uint32_t *request)
{
	const struct kl_settings_key *found;
	char fallback[16];
	uint32_t number;
	int error;

	/* Only this application's keys, with its file. */
	if (!settings->app.used)
		return ENOTSUP;

	/* The file. */
	error = settings_app_write(&settings->app, key, value);
	if (error != 0)
		return error;

	/* The value in effect: the one asked for, the default of a row's key, or none for a prefix row's. */
	found = kl_settings_key_find(key);
	if (value != NULL) {
		settings_cache_set(&settings->cache, key, value, 0U, 1U);
	} else if (found != NULL && (found->flags & KL_SETTINGS_KEY_PREFIX) != 0U) {
		settings_cache_set(&settings->cache, key, "", 0U, 0U);
	} else if (found != NULL) {
		(void)snprintf(fallback, sizeof(fallback), "%d", found->fallback);
		settings_cache_set(&settings->cache, key, fallback, KL_SETTINGS_DEFAULT, 1U);
	}

	/* The request is finished already. */
	number = settings->next_request;
	settings->next_request++;
	settings_cache_result(&settings->cache, number, 0);

	/* Succeeded: the key holds its value. */
	if (request != NULL)
		*request = number;
	return 0;
}
