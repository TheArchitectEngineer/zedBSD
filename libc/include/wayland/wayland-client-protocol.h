/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Declares the selected core protocol objects and typed requests. */

#ifndef ZEDBSD_WAYLAND_CLIENT_PROTOCOL_H
#define ZEDBSD_WAYLAND_CLIENT_PROTOCOL_H

#include <wayland/wayland-client-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_registry;
struct wl_callback;
struct wl_compositor;
struct wl_surface;
struct wl_region;
struct wl_buffer;
struct wl_output;
struct xdg_wm_base;
struct xdg_positioner;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_popup;
struct zed_gpu_buffer_v1;

struct wl_display;
extern const struct wl_interface wl_display_interface;

/* Receives events for one wl_display object; retained by its proxy. */
struct wl_display_listener {
	void (*error)(void *data, struct wl_display *object, struct wl_proxy *object_id, uint32_t code, const char *message);
	void (*delete_id)(void *data, struct wl_display *object, uint32_t id);
};

int wl_display_add_listener(struct wl_display *object, const struct wl_display_listener *listener, void *data);
#define WL_DISPLAY_SYNC 0U
struct wl_callback *wl_display_sync(struct wl_display *object);
#define WL_DISPLAY_GET_REGISTRY 1U
struct wl_registry *wl_display_get_registry(struct wl_display *object);
void wl_display_set_user_data(struct wl_display *object, void *data);
void *wl_display_get_user_data(struct wl_display *object);
uint32_t wl_display_get_version(struct wl_display *object);

struct wl_registry;
extern const struct wl_interface wl_registry_interface;

/* Receives events for one wl_registry object; retained by its proxy. */
struct wl_registry_listener {
	void (*global)(void *data, struct wl_registry *object, uint32_t name, const char *interface_name, uint32_t version);
	void (*global_remove)(void *data, struct wl_registry *object, uint32_t name);
};

int wl_registry_add_listener(struct wl_registry *object, const struct wl_registry_listener *listener, void *data);
void wl_registry_destroy(struct wl_registry *object);
void wl_registry_set_user_data(struct wl_registry *object, void *data);
void *wl_registry_get_user_data(struct wl_registry *object);
uint32_t wl_registry_get_version(struct wl_registry *object);

struct wl_callback;
extern const struct wl_interface wl_callback_interface;

/* Receives events for one wl_callback object; retained by its proxy. */
struct wl_callback_listener {
	void (*done)(void *data, struct wl_callback *object, uint32_t callback_data);
};

int wl_callback_add_listener(struct wl_callback *object, const struct wl_callback_listener *listener, void *data);
void wl_callback_destroy(struct wl_callback *object);
void wl_callback_set_user_data(struct wl_callback *object, void *data);
void *wl_callback_get_user_data(struct wl_callback *object);
uint32_t wl_callback_get_version(struct wl_callback *object);

struct wl_compositor;
extern const struct wl_interface wl_compositor_interface;
#define WL_COMPOSITOR_CREATE_SURFACE 0U
struct wl_surface *wl_compositor_create_surface(struct wl_compositor *object);
#define WL_COMPOSITOR_CREATE_REGION 1U
struct wl_region *wl_compositor_create_region(struct wl_compositor *object);
void wl_compositor_destroy(struct wl_compositor *object);
void wl_compositor_set_user_data(struct wl_compositor *object, void *data);
void *wl_compositor_get_user_data(struct wl_compositor *object);
uint32_t wl_compositor_get_version(struct wl_compositor *object);

struct wl_surface;
extern const struct wl_interface wl_surface_interface;

/* Receives events for one wl_surface object; retained by its proxy. */
struct wl_surface_listener {
	void (*enter)(void *data, struct wl_surface *object, struct wl_output *output);
	void (*leave)(void *data, struct wl_surface *object, struct wl_output *output);
};

int wl_surface_add_listener(struct wl_surface *object, const struct wl_surface_listener *listener, void *data);
#define WL_SURFACE_DESTROY 0U
void wl_surface_destroy(struct wl_surface *object);
#define WL_SURFACE_ATTACH 1U
void wl_surface_attach(struct wl_surface *object, struct wl_buffer *buffer, int32_t x, int32_t y);
#define WL_SURFACE_DAMAGE 2U
void wl_surface_damage(struct wl_surface *object, int32_t x, int32_t y, int32_t width, int32_t height);
#define WL_SURFACE_FRAME 3U
struct wl_callback *wl_surface_frame(struct wl_surface *object);
#define WL_SURFACE_SET_OPAQUE_REGION 4U
void wl_surface_set_opaque_region(struct wl_surface *object, struct wl_region *region);
#define WL_SURFACE_SET_INPUT_REGION 5U
void wl_surface_set_input_region(struct wl_surface *object, struct wl_region *region);
#define WL_SURFACE_COMMIT 6U
void wl_surface_commit(struct wl_surface *object);
#define WL_SURFACE_SET_BUFFER_TRANSFORM 7U
void wl_surface_set_buffer_transform(struct wl_surface *object, int32_t transform);
#define WL_SURFACE_SET_BUFFER_SCALE 8U
void wl_surface_set_buffer_scale(struct wl_surface *object, int32_t scale);
#define WL_SURFACE_DAMAGE_BUFFER 9U
void wl_surface_damage_buffer(struct wl_surface *object, int32_t x, int32_t y, int32_t width, int32_t height);
void wl_surface_set_user_data(struct wl_surface *object, void *data);
void *wl_surface_get_user_data(struct wl_surface *object);
uint32_t wl_surface_get_version(struct wl_surface *object);

struct wl_region;
extern const struct wl_interface wl_region_interface;
#define WL_REGION_DESTROY 0U
void wl_region_destroy(struct wl_region *object);
#define WL_REGION_ADD 1U
void wl_region_add(struct wl_region *object, int32_t x, int32_t y, int32_t width, int32_t height);
#define WL_REGION_SUBTRACT 2U
void wl_region_subtract(struct wl_region *object, int32_t x, int32_t y, int32_t width, int32_t height);
void wl_region_set_user_data(struct wl_region *object, void *data);
void *wl_region_get_user_data(struct wl_region *object);
uint32_t wl_region_get_version(struct wl_region *object);

struct wl_buffer;
extern const struct wl_interface wl_buffer_interface;

/* Receives events for one wl_buffer object; retained by its proxy. */
struct wl_buffer_listener {
	void (*release)(void *data, struct wl_buffer *object);
};

int wl_buffer_add_listener(struct wl_buffer *object, const struct wl_buffer_listener *listener, void *data);
#define WL_BUFFER_DESTROY 0U
void wl_buffer_destroy(struct wl_buffer *object);
void wl_buffer_set_user_data(struct wl_buffer *object, void *data);
void *wl_buffer_get_user_data(struct wl_buffer *object);
uint32_t wl_buffer_get_version(struct wl_buffer *object);

struct wl_output;
extern const struct wl_interface wl_output_interface;

/* Receives events for one wl_output object; retained by its proxy. */
struct wl_output_listener {
	void (*geometry)(void *data, struct wl_output *object, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform);
	void (*mode)(void *data, struct wl_output *object, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
	void (*done)(void *data, struct wl_output *object);
	void (*scale)(void *data, struct wl_output *object, int32_t factor);
	void (*name)(void *data, struct wl_output *object, const char *name);
	void (*description)(void *data, struct wl_output *object, const char *description);
};

int wl_output_add_listener(struct wl_output *object, const struct wl_output_listener *listener, void *data);
#define WL_OUTPUT_RELEASE 0U
void wl_output_release(struct wl_output *object);
void wl_output_set_user_data(struct wl_output *object, void *data);
void *wl_output_get_user_data(struct wl_output *object);
uint32_t wl_output_get_version(struct wl_output *object);

#define WL_REGISTRY_BIND 0U
void *wl_registry_bind(struct wl_registry *registry, uint32_t name, const struct wl_interface *interface, uint32_t version);

#define WL_OUTPUT_MODE_CURRENT 1U
#define WL_OUTPUT_MODE_PREFERRED 2U
#define WL_OUTPUT_TRANSFORM_NORMAL 0
#define WL_OUTPUT_SUBPIXEL_UNKNOWN 0

#ifdef __cplusplus
}
#endif

#endif
