/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The wp_content_type_v1 requests mview calls (Keiland's libwayland has
 * them as functions, content-type-v1-client-protocol.h), over the system
 * libwayland-client, for mview's Linux build of ws291 (build-mview.sh).
 * The interfaces come from wayland-scanner's private code.
 */

#include <wayland-client.h>

#include "userland/desktop/libwayland/content-type-v1-client-protocol.h"

/*
 * Destroys the manager.
 */
void
wp_content_type_manager_v1_destroy(
	struct wp_content_type_manager_v1 *manager)
{
	/* The destroy request, which also frees the proxy. */
	wl_proxy_marshal_flags((struct wl_proxy *)manager, WP_CONTENT_TYPE_MANAGER_V1_DESTROY, NULL, wl_proxy_get_version((struct wl_proxy *)manager), WL_MARSHAL_FLAG_DESTROY);

	/* Succeeded: sent. */
	return;
}

/*
 * Makes the content type object of a surface.
 */
struct wp_content_type_v1 *
wp_content_type_manager_v1_get_surface_content_type(
	struct wp_content_type_manager_v1 *manager,
	struct wl_surface *surface)
{
	struct wl_proxy *object;

	/* The new object, with the surface it describes. */
	object = wl_proxy_marshal_flags((struct wl_proxy *)manager, WP_CONTENT_TYPE_MANAGER_V1_GET_SURFACE_CONTENT_TYPE, &wp_content_type_v1_interface, wl_proxy_get_version((struct wl_proxy *)manager), 0U, NULL, surface);

	/* Succeeded: the object, or NULL when it could not be made. */
	return (struct wp_content_type_v1 *)object;
}

/*
 * Destroys a surface's content type object.
 */
void
wp_content_type_v1_destroy(
	struct wp_content_type_v1 *content)
{
	/* The destroy request, which also frees the proxy. */
	wl_proxy_marshal_flags((struct wl_proxy *)content, WP_CONTENT_TYPE_V1_DESTROY, NULL, wl_proxy_get_version((struct wl_proxy *)content), WL_MARSHAL_FLAG_DESTROY);

	/* Succeeded: sent. */
	return;
}

/*
 * Sets the surface's content type, applied with its next commit.
 */
void
wp_content_type_v1_set_content_type(
	struct wp_content_type_v1 *content,
	uint32_t type)
{
	/* The set_content_type request. */
	wl_proxy_marshal_flags((struct wl_proxy *)content, WP_CONTENT_TYPE_V1_SET_CONTENT_TYPE, NULL, wl_proxy_get_version((struct wl_proxy *)content), 0U, type);

	/* Succeeded: sent. */
	return;
}
