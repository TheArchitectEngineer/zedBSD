/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The glass panels (ws035-p083, plan/ws035/glass-design.md): the wrapper
 * of zdesktop's keiland_glass_v1 protocol.
 *
 * A list of panels is checked here against the compositor's bounds before
 * it is sent, so that a list the compositor would refuse -- with a
 * protocol error that ends the whole connection -- is refused as one
 * failed call instead.  The protocol has no events.
 */

#include <keiland.h>

#include <wayland-client.h>
#include "userland/desktop/libwayland/zed-glass-v1-client-protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The version of the protocol this library speaks (2: set_blur, ws075-p029). */
#define GLASS_VERSION		2U

/* One panel on the wire: six words. */
#define GLASS_PANEL_WORDS	6U

/*
 * One surface's glass: its keiland_glass_v1.
 */
struct keiland_glass {
	struct keiland_glass_v1 *proxy;
	uint32_t version;
};

/* What the registry search found: the manager's global name (0 for none) and version. */
struct glass_search {
	uint32_t name;
	uint32_t version;
};

static struct keiland_glass_manager_v1 *glass_bind(struct wl_display *display, uint32_t *version);
static void glass_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void glass_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static int glass_check(const struct keiland_glass_panel *panel);

/* The registry's callbacks while the manager is looked for. */
static const struct wl_registry_listener glass_registry_listener = {
	glass_global, glass_global_remove
};

/*
 * Gives a surface its glass, with no panels yet.  Returns NULL with errno
 * set: ENOTSUP for a compositor without glass, ENOMEM when the objects
 * cannot be made.
 */
struct keiland_glass *
keiland_glass_create(
	struct wl_display *display,
	struct wl_surface *surface)
{
	struct keiland_glass_manager_v1 *manager;
	struct keiland_glass *glass;
	uint32_t version;

	/* zdesktop's manager, bound for this surface. */
	manager = glass_bind(display, &version);
	if (manager == NULL)
		return NULL;

	/* The record. */
	glass = calloc(1, sizeof(*glass));
	if (glass == NULL) {
		keiland_glass_manager_v1_destroy(manager);
		errno = ENOMEM;
		return NULL;
	}

	/* The protocol object (of the manager's version); the binding is not needed after it (the glass stays). */
	glass->version = version;
	glass->proxy = keiland_glass_manager_v1_get_glass(manager, surface);
	keiland_glass_manager_v1_destroy(manager);
	if (glass->proxy == NULL) {
		free(glass);
		errno = ENOMEM;
		return NULL;
	}

	/* Succeeded: the surface can be given panels. */
	return glass;
}

/*
 * Sets the surface's panels for its next commit.
 */
int
keiland_glass_set_panels(
	struct keiland_glass *glass,
	const struct keiland_glass_panel *panels,
	size_t count)
{
	int32_t words[KEILAND_GLASS_PANELS_MAX * GLASS_PANEL_WORDS];
	struct wl_array array;
	size_t index;
	int error;

	/* No more panels than the compositor keeps. */
	if (count > KEILAND_GLASS_PANELS_MAX)
		return E2BIG;

	/* Each panel checked and laid out as its six words. */
	for (index = 0; index < count; index++) {
		error = glass_check(&panels[index]);
		if (error != 0)
			return error;
		words[index * GLASS_PANEL_WORDS] = panels[index].x;
		words[index * GLASS_PANEL_WORDS + 1U] = panels[index].y;
		words[index * GLASS_PANEL_WORDS + 2U] = panels[index].width;
		words[index * GLASS_PANEL_WORDS + 3U] = panels[index].height;
		words[index * GLASS_PANEL_WORDS + 4U] = panels[index].radius;
		words[index * GLASS_PANEL_WORDS + 5U] = (int32_t)panels[index].kind;
	}

	/* The list as the request's array (the marshaller copies it). */
	array.size = count * GLASS_PANEL_WORDS * sizeof(words[0]);
	array.alloc = array.size;
	array.data = words;
	keiland_glass_v1_set_panels(glass->proxy, &array);

	/* Succeeded: the panels go with the next commit. */
	return 0;
}

/*
 * Chooses whether the surface's glass shows the windows under it blurred
 * (enabled) or only the blurred wallpaper (the default, which costs the
 * compositor nothing per frame), from the surface's next commit
 * (ws075-p029).  Returns ENOTSUP when the compositor's glass is older.
 */
int
keiland_glass_set_blur(
	struct keiland_glass *glass,
	int enabled)
{
	uint32_t value;

	/* A compositor whose glass has no choice. */
	if (glass->version < KEILAND_GLASS_V1_SET_BLUR_SINCE_VERSION)
		return ENOTSUP;

	/* The choice, for the next commit. */
	value = 0U;
	if (enabled != 0)
		value = 1U;
	keiland_glass_v1_set_blur(glass->proxy, value);

	/* Succeeded. */
	return 0;
}

/*
 * Takes the glass away; the surface's next commit shows it without panels.
 */
void
keiland_glass_destroy(
	struct keiland_glass *glass)
{
	/* No glass, nothing to destroy. */
	if (glass == NULL)
		return;

	/* The protocol object, then the record. */
	keiland_glass_v1_destroy(glass->proxy);
	free(glass);
}

/* Binds zdesktop's glass manager through a registry of the library's own. */
static struct keiland_glass_manager_v1 *
glass_bind(
	struct wl_display *display,
	uint32_t *version)
{
	struct keiland_glass_manager_v1 *manager;
	struct glass_search search;
	struct wl_event_queue *queue;
	struct wl_display *wrapper;
	struct wl_registry *registry;
	int status;

	/* The search's own queue, and the display as seen from it. */
	queue = wl_display_create_queue(display);
	if (queue == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* The display as the search sees it. */
	wrapper = wl_proxy_create_wrapper(display);
	if (wrapper == NULL) {
		wl_event_queue_destroy(queue);
		errno = ENOMEM;
		return NULL;
	}

	/* What the wrapper makes lives on the search's queue. */
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

	/* The globals, announced to this search alone. */
	search.name = 0;
	search.version = 0;
	registry = wl_display_get_registry(wrapper);
	if (registry != NULL) {
		status = wl_registry_add_listener(registry, &glass_registry_listener, &search);
		if (status == 0)
			(void)wl_display_roundtrip_queue(display, queue);
	}

	/* The manager, bound when announced at the version both speak, is moved to the application's default queue. */
	manager = NULL;
	*version = search.version;
	if (*version > GLASS_VERSION)
		*version = GLASS_VERSION;
	if (registry != NULL && search.name != 0U) {
		manager = wl_registry_bind(registry, search.name, &keiland_glass_manager_v1_interface, *version);
		if (manager != NULL)
			wl_proxy_set_queue((struct wl_proxy *)manager, NULL);
	}

	/* The search's objects go. */
	if (registry != NULL)
		wl_registry_destroy(registry);
	wl_proxy_wrapper_destroy(wrapper);
	wl_event_queue_destroy(queue);

	/* A compositor without glass. */
	if (search.name == 0U) {
		errno = ENOTSUP;
		return NULL;
	}

	/* The binding could not be made. */
	if (manager == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* Succeeded: the manager. */
	return manager;
}

/* Notes the manager's global name when the registry announces it. */
static void
glass_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct glass_search *search;
	int match;

	/* Only zdesktop's glass manager is looked for. */
	(void)registry;
	search = data;
	match = strcmp(interface, "keiland_glass_manager_v1");
	if (match == 0) {
		search->name = name;
		search->version = version;
	}
}

/* A global going away during the short search changes nothing. */
static void
glass_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to forget. */
	(void)data;
	(void)registry;
	(void)name;
}

/* Checks one panel against the compositor's bounds. */
static int
glass_check(
	const struct keiland_glass_panel *panel)
{
	/* An empty panel. */
	if (panel->width <= 0 || panel->height <= 0)
		return EINVAL;

	/* A radius that is negative or past the largest. */
	if (panel->radius < 0 || panel->radius > KEILAND_GLASS_RADIUS_MAX)
		return EINVAL;

	/* A kind that does not exist. */
	if (panel->kind != KEILAND_GLASS_CARD)
		return EINVAL;

	/* Succeeded. */
	return 0;
}
