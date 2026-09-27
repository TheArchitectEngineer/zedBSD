/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declares zdesktop's glass protocol (zed_glass_v1, ws035-p083).
 *
 * The header is private: it is not installed, and applications reach the
 * protocol through libkeiland (<zdesktop.h>) only.  libkeiland includes
 * it by its path in the tree.  The protocol is defined in
 * plan/ws035/glass-design.md.
 */

#ifndef ZEDBSD_ZED_GLASS_V1_CLIENT_PROTOCOL_H
#define ZEDBSD_ZED_GLASS_V1_CLIENT_PROTOCOL_H

#include <wayland/wayland-client-core.h>
#include <wayland/wayland-client-protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The objects the protocol names; only libwayland knows what is in them. */
struct wl_array;
struct wl_surface;
struct zed_glass_manager_v1;
struct zed_glass_v1;

/* The two interfaces' descriptions (glass-protocol.c). */
extern const struct wl_interface zed_glass_manager_v1_interface;
extern const struct wl_interface zed_glass_v1_interface;

/* zed_glass_manager_v1: the global that gives surfaces their glass. */
#define ZED_GLASS_MANAGER_V1_ERROR_ALREADY_EXISTS 0U
#define ZED_GLASS_MANAGER_V1_DESTROY 0U
#define ZED_GLASS_MANAGER_V1_GET_GLASS 1U
void zed_glass_manager_v1_destroy(struct zed_glass_manager_v1 *object);
struct zed_glass_v1 *zed_glass_manager_v1_get_glass(struct zed_glass_manager_v1 *object, struct wl_surface *surface);

/* zed_glass_v1: one surface's panels on the system's frosted glass. */
#define ZED_GLASS_V1_ERROR_BAD_PANELS 0U
#define ZED_GLASS_V1_ERROR_NO_SURFACE 1U
#define ZED_GLASS_V1_KIND_CARD 0U
#define ZED_GLASS_V1_PANELS_MAX 32U
#define ZED_GLASS_V1_RADIUS_MAX 64
#define ZED_GLASS_V1_DESTROY 0U
#define ZED_GLASS_V1_SET_PANELS 1U
void zed_glass_v1_destroy(struct zed_glass_v1 *object);
void zed_glass_v1_set_panels(struct zed_glass_v1 *object, struct wl_array *panels);

#ifdef __cplusplus
}
#endif

#endif
