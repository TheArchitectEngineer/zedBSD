/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Describes and marshals zdesktop's appearance protocol (keiland_theme_v1,
 * version 1; ws089-p017): the desktop's appearance, light or dark.
 */

#include "internal.h"

#include "userland/desktop/libwayland/zed-theme-v1-client-protocol.h"

/* The argument types of the protocol's messages (none names an interface). */
static const struct wl_interface *theme_plain_types[] = {
	NULL,
};

/* The requests of keiland_theme_v1, in wire opcode order. */
static const struct wl_message theme_requests[] = {
	{ "destroy", "", NULL },
};

/* The events of keiland_theme_v1, in wire opcode order. */
static const struct wl_message theme_events[] = {
	{ "appearance", "u", theme_plain_types },
};

/* The immutable keiland_theme_v1 description. */
const struct wl_interface keiland_theme_v1_interface = {
	"keiland_theme_v1", 1, 1, theme_requests,
	1, theme_events
};

/*
 * Installs a listener of keiland_theme_v1 (appearance).
 */
int
keiland_theme_v1_add_listener(
	struct keiland_theme_v1 *object,
	const struct keiland_theme_v1_listener *listener,
	void *data)
{
	int error;

	/* The callback receives the object's events from now on. */
	error = wl_proxy_add_listener((struct wl_proxy *)object, (void (**)(void))listener, data);
	if (error != 0)
		return error;

	/* Succeeded: the listener is installed. */
	return 0;
}

/*
 * Sends keiland_theme_v1.destroy: no more appearance events.
 */
void
keiland_theme_v1_destroy(
	struct keiland_theme_v1 *object)
{
	/* Queues the destructor and retires the proxy. */
	wl_proxy_marshal_array_flags((struct wl_proxy *)object, KEILAND_THEME_V1_DESTROY, NULL, 0, WL_MARSHAL_FLAG_DESTROY, NULL);
}
