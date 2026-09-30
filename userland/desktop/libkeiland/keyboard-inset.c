/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The keyboard inset (ws102-p015, plan/ws102/design.md section 2.8): the
 * wrapper of zdesktop's keiland_keyboard_inset_v1 protocol.  A window hears
 * how much of it the on-screen keyboard covers, from its right and bottom
 * edges, when the keyboard opens, closes or changes the window.  With a
 * compositor that does not have the protocol nothing is made (ENOTSUP) and
 * the window hears nothing, as before.
 */

#include <keiland.h>

#include <wayland-client.h>
#include "userland/desktop/libwayland/zed-keyboard-inset-v1-client-protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The version of the protocol this library speaks. */
#define INSET_VERSION		1U

/*
 * One window's inset: its keiland_keyboard_inset_v1, and the application's
 * callback and its data.
 */
struct keiland_keyboard_inset {
	struct keiland_keyboard_inset_v1 *proxy;
	keiland_keyboard_inset_fn callback;
	void *data;
};

/* What the registry search found: the manager's global name, 0 for none. */
struct inset_search {
	uint32_t name;
};

static struct keiland_keyboard_inset_manager_v1 *inset_bind(struct wl_display *display);
static void inset_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void inset_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void inset_event(void *data, struct keiland_keyboard_inset_v1 *object, int32_t right, int32_t bottom, uint32_t reason);

/* The registry's callbacks while the manager is looked for. */
static const struct wl_registry_listener inset_registry_listener = {
	inset_global, inset_global_remove
};

/* The inset's callback. */
static const struct keiland_keyboard_inset_v1_listener inset_listener = {
	inset_event
};

/*
 * Asks for a window's keyboard inset: callback hears each change on the
 * application's default queue.  Returns NULL with errno set: ENOTSUP for a
 * compositor without the protocol, EINVAL, ENOMEM.
 */
struct keiland_keyboard_inset *
keiland_keyboard_inset_create(
	struct wl_display *display,
	struct xdg_toplevel *toplevel,
	keiland_keyboard_inset_fn callback,
	void *data)
{
	struct keiland_keyboard_inset_manager_v1 *manager;
	struct keiland_keyboard_inset *inset;
	int error;

	/* A window and a callback. */
	if (display == NULL || toplevel == NULL || callback == NULL) {
		errno = EINVAL;
		return NULL;
	}

	/* zdesktop's manager, bound for this window. */
	manager = inset_bind(display);
	if (manager == NULL)
		return NULL;

	/* The record. */
	inset = calloc(1, sizeof(*inset));
	if (inset == NULL) {
		keiland_keyboard_inset_manager_v1_destroy(manager);
		errno = ENOMEM;
		return NULL;
	}
	inset->callback = callback;
	inset->data = data;

	/* The protocol object; the binding is not needed after it (the inset stays). */
	inset->proxy = keiland_keyboard_inset_manager_v1_get_inset(manager, toplevel);
	keiland_keyboard_inset_manager_v1_destroy(manager);
	if (inset->proxy == NULL) {
		free(inset);
		errno = ENOMEM;
		return NULL;
	}

	/* Its events come to the callback. */
	error = keiland_keyboard_inset_v1_add_listener(inset->proxy, &inset_listener, inset);
	if (error != 0) {
		keiland_keyboard_inset_v1_destroy(inset->proxy);
		free(inset);
		errno = ENOMEM;
		return NULL;
	}

	/* Succeeded: the window hears the keyboard. */
	return inset;
}

/*
 * Stops hearing the keyboard: the protocol object and the record go.
 */
void
keiland_keyboard_inset_destroy(
	struct keiland_keyboard_inset *inset)
{
	/* No inset, nothing to destroy. */
	if (inset == NULL)
		return;

	/* The protocol object, then the record. */
	keiland_keyboard_inset_v1_destroy(inset->proxy);
	free(inset);
}

/* Passes an inset event to the application's callback. */
static void
inset_event(
	void *data,
	struct keiland_keyboard_inset_v1 *object,
	int32_t right,
	int32_t bottom,
	uint32_t reason)
{
	struct keiland_keyboard_inset *inset;

	/* The record the listener was given. */
	(void)object;
	inset = data;

	/* The callback. */
	inset->callback(inset->data, right, bottom, reason);
}

/* Binds zdesktop's inset manager through a registry of the library's own. */
static struct keiland_keyboard_inset_manager_v1 *
inset_bind(
	struct wl_display *display)
{
	struct keiland_keyboard_inset_manager_v1 *manager;
	struct inset_search search;
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
	registry = wl_display_get_registry(wrapper);
	if (registry != NULL) {
		status = wl_registry_add_listener(registry, &inset_registry_listener, &search);
		if (status == 0)
			(void)wl_display_roundtrip_queue(display, queue);
	}

	/* The manager, bound when announced, is moved to the application's default queue. */
	manager = NULL;
	if (registry != NULL && search.name != 0U) {
		manager = wl_registry_bind(registry, search.name, &keiland_keyboard_inset_manager_v1_interface, INSET_VERSION);
		if (manager != NULL)
			wl_proxy_set_queue((struct wl_proxy *)manager, NULL);
	}

	/* The search's objects go. */
	if (registry != NULL)
		wl_registry_destroy(registry);
	wl_proxy_wrapper_destroy(wrapper);
	wl_event_queue_destroy(queue);

	/* A compositor without the protocol. */
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
inset_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct inset_search *search;
	int match;

	/* Only zdesktop's inset manager is looked for. */
	(void)registry;
	(void)version;
	search = data;
	match = strcmp(interface, "keiland_keyboard_inset_manager_v1");
	if (match == 0)
		search->name = name;
}

/* A global going away during the short search changes nothing. */
static void
inset_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to forget. */
	(void)data;
	(void)registry;
	(void)name;
}
