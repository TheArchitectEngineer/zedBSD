/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Describes and marshals zdesktop's glass protocol (ws035-p083).
 *
 * zed_glass_manager_v1 gives a surface its glass (zed_glass_v1): the list
 * of the surface's panels -- cards floating in the window -- under which
 * zdesktop draws the system's frosted glass.  The protocol is zdesktop's own; its header is private and
 * applications use it through libkeiland.  plan/ws035/glass-design.md
 * defines every request.
 */

#include "internal.h"

/* The argument of zed_glass_v1.set_panels names no interface. */
static const struct wl_interface *glass_plain_types[] = {
	NULL,
};

/* The arguments of zed_glass_manager_v1.get_glass: the new glass, and the surface. */
static const struct wl_interface *glass_manager_get_types[] = {
	&zed_glass_v1_interface,
	&wl_surface_interface,
};

/* The requests of zed_glass_manager_v1, in wire order. */
static const struct wl_message glass_manager_requests[] = {
	{ "destroy", "", NULL },
	{ "get_glass", "no", glass_manager_get_types },
};

/* Describes the global that gives surfaces their glass. */
const struct wl_interface zed_glass_manager_v1_interface = {
	"zed_glass_manager_v1", 1, 2, glass_manager_requests,
	0, NULL
};

/* The requests of zed_glass_v1, in wire order. */
static const struct wl_message glass_requests[] = {
	{ "destroy", "", NULL },
	{ "set_panels", "a", glass_plain_types },
};

/* Describes one surface's glass. */
const struct wl_interface zed_glass_v1_interface = {
	"zed_glass_v1", 1, 2, glass_requests,
	0, NULL
};

/*
 * Sends zed_glass_manager_v1.destroy; the glass it gave stays.
 */
void
zed_glass_manager_v1_destroy(
	struct zed_glass_manager_v1 *object)
{
	/* Queues the destructor and retires the proxy. */
	wl_proxy_marshal_array_flags((struct wl_proxy *)object, ZED_GLASS_MANAGER_V1_DESTROY, NULL, 0, WL_MARSHAL_FLAG_DESTROY, NULL);
}

/*
 * Sends zed_glass_manager_v1.get_glass and returns the surface's new glass.
 */
struct zed_glass_v1 *
zed_glass_manager_v1_get_glass(
	struct zed_glass_manager_v1 *object,
	struct wl_surface *surface)
{
	union wl_argument arguments[2];
	struct wl_proxy *created;

	/* The new glass's identity, then the surface it belongs to. */
	arguments[0].n = 0;
	arguments[1].o = (struct wl_object *)surface;

	/* Queues the request together with the new proxy. */
	created = wl_proxy_marshal_array_flags((struct wl_proxy *)object, ZED_GLASS_MANAGER_V1_GET_GLASS, &zed_glass_v1_interface, 1U, 0, arguments);
	if (created == NULL)
		return NULL;

	/* Succeeded: the caller owns the new glass. */
	return (struct zed_glass_v1 *)created;
}

/*
 * Sends zed_glass_v1.destroy: the surface's next commit shows it without
 * panels.
 */
void
zed_glass_v1_destroy(
	struct zed_glass_v1 *object)
{
	/* Queues the destructor and retires the proxy. */
	wl_proxy_marshal_array_flags((struct wl_proxy *)object, ZED_GLASS_V1_DESTROY, NULL, 0, WL_MARSHAL_FLAG_DESTROY, NULL);
}

/*
 * Sends zed_glass_v1.set_panels: the surface's panels for its next commit,
 * six words each (x, y, width, height, radius, kind).
 */
void
zed_glass_v1_set_panels(
	struct zed_glass_v1 *object,
	struct wl_array *panels)
{
	union wl_argument arguments[1];

	/* The list of panels. */
	arguments[0].a = panels;
	wl_proxy_marshal_array_flags((struct wl_proxy *)object, ZED_GLASS_V1_SET_PANELS, NULL, 0, 0, arguments);
}
