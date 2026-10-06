/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of Settings that speak Wayland and Vulkan: the window
 * (window.c), the presenter of drawn frames (present.c), the menus
 * (menu.c), the titlebar (titlebar.c) and the glass (glass.c).  They are
 * the file manager's (ws071), copied and cut down to what Settings uses;
 * the host tests build the rest of the program without them.
 */

#ifndef KEILAND_SETTINGS_WINDOW_H
#define KEILAND_SETTINGS_WINDOW_H

#include "settings.h"

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <keiland.h>

/* How many inputs wait for the main loop at most. */
#define SE_WINDOW_EVENTS	256U

/*
 * The Wayland window: its globals, its surface and roles, the size the
 * compositor gave it, the screen it is on, and the input waiting for the
 * interface.
 *
 * One lives for the whole run.
 */
struct se_window {
	/* The connection and the globals bound from it. */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;

	/*
	 * The first screen and its current mode as the compositor reports it
	 * (About and Display show it; 0 while unknown).
	 */
	struct wl_output *output;
	int32_t output_width;
	int32_t output_height;
	int32_t output_refresh;

	/* The window: its surface and roles. */
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;

	/* The size the compositor asked for, and whether it changed since it was last taken. */
	uint32_t width;
	uint32_t height;
	int resized;

	/* The largest size the window may choose for itself (xdg-shell's bounds; 0 when not known), and the size it would like. */
	uint32_t bounds_width;
	uint32_t bounds_height;
	uint32_t preferred_width;
	uint32_t preferred_height;

	/* Whether the first configure arrived, the compositor asked to close, the window is maximized. */
	int configured;
	int closed;
	int maximized;

	/* The pointer's place, the serial of its last press, and the modifiers held (SE_MOD_*). */
	int pointer_x;
	int pointer_y;
	uint32_t button_serial;
	uint32_t modifiers;

	/* The finger taken as the pointer (-1 when none): a touch is a click where it lands and lifts. */
	int32_t touch_id;

	/*
	 * The scrolling of the pointer's frame (BUG-211): what it comes from
	 * (SE_SOURCE_*, a wheel's until told, and again after the frame),
	 * whether the frame told the fingers' end already, and the fraction of
	 * a pixel not yet given (a touch pad scrolls a unit at a time).
	 */
	unsigned axis_source;
	int axis_stopped;
	double axis_remainder;

	/* The key held for repeating (0 when none), when it repeats next, and the repeat's delay and interval. */
	uint32_t repeat_key;
	uint64_t repeat_at;
	uint32_t repeat_delay;
	uint32_t repeat_interval;

	/* The inputs waiting, a ring: the oldest's slot and how many. */
	struct se_event events[SE_WINDOW_EVENTS];
	unsigned event_first;
	unsigned event_count;

	/*
	 * Another descriptor the wait wakes for, -1 for none: the one copy's
	 * socket, which a later start of Settings makes readable (ws089-p016).
	 */
	int extra_fd;
};

/*
 * The window's menus as given to zdesktop (menu.c): the connection's menu
 * service (NULL when the compositor has none, and the window then has no
 * menus), the menu and the window's place for it, the state the menus last
 * showed, and the window whose inputs the choices join (SE_EVENT_ACTION).
 */
struct se_menu {
	struct keiland_menu_service *service;
	struct keiland_menu *menu;
	struct keiland_window_menu *window_menu;
	struct se_menu_state shown;
	int sent;
	struct se_window *window;
};

/*
 * The window's titlebar as given to zdesktop (titlebar.c): zdesktop's
 * titlebar object, the state it last showed (and whether it was ever
 * sent), and what the user did with it and the main loop has not yet
 * carried out, oldest first.
 */
struct se_titlebar {
	struct se_window *window;
	struct keiland_titlebar *titlebar;
	struct se_titlebar_state shown;
	int sent;
	struct se_titlebar_event events[SE_TITLEBAR_EVENTS];
	unsigned event_count;
};

/*
 * One swapchain image the presenter draws into; the image is the
 * swapchain's.
 */
struct se_present_target {
	VkImage image;
	VkImageView view;
	VkFramebuffer framebuffer;
	VkSemaphore rendered;
};

/*
 * The Vulkan objects that show the drawn frames in the window: a
 * swapchain, and a host-written canvas image the size of the window that
 * one quad copies onto each swapchain image.
 */
struct se_present {
	/* The instance, the surface of the window, the device and its queue, and the device's name (About shows it). */
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

	/* The swapchain, its format and extent, and one target per image. */
	VkSwapchainKHR swapchain;
	VkFormat format;
	/* Whether the swapchain is see-through: zdesktop blends the frame by its premultiplied alpha. */
	int premultiplied;
	VkExtent2D extent;
	struct se_present_target *targets;
	uint32_t count;

	/* The pass and the pipeline that draw the canvas, and what the pipeline binds. */
	VkRenderPass pass;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet set;
	VkSampler sampler;

	/* The canvas image (linear, host-written), its memory, view and map, and whether it was made ready. */
	VkImage canvas;
	VkDeviceMemory canvas_memory;
	VkImageView canvas_view;
	unsigned char *canvas_map;
	size_t canvas_pitch;
	int canvas_ready;

	/* The quad's vertices, in host-visible memory. */
	VkBuffer vertices;
	VkDeviceMemory vertex_memory;

	/* One command buffer, the fence that says it finished and the acquire semaphore. */
	VkCommandPool pool;
	VkCommandBuffer command;
	VkFence fence;
	VkSemaphore acquired;

	/* The Vulkan call that failed last, for the error line; the last frame's copy, acquire, present and wait times (milliseconds). */
	const char *operation;
	unsigned copy_ms;
	unsigned acquire_ms;
	unsigned present_ms;
	unsigned wait_ms;
};

/*
 * The window's glass in zdesktop (glass.c): zdesktop's glass object (NULL
 * when the window is not glass) and the panels it last sent.
 */
struct se_glass {
	struct keiland_glass *glass;
	struct se_panel shown[SE_PANELS];
	size_t shown_count;
	int sent;
};

/* The window (window.c). */
int se_window_open(struct se_window *window, const char *display, uint32_t width, uint32_t height, const char *title, const char *application);
int se_window_dispatch(struct se_window *window, int timeout);
int se_window_take(struct se_window *window, struct se_event *event);
int se_window_repeat(struct se_window *window, uint64_t now);
void se_window_close(struct se_window *window);
void se_window_action(struct se_window *window, uint32_t action);
void se_window_minimize(struct se_window *window);
void se_window_zoom(struct se_window *window);
struct se_event *se_window_push(struct se_window *window, unsigned type);
uint64_t se_clock(void);

/* The menus (menu.c). */
int se_menu_open(struct se_menu *menu, struct se_window *window, const struct se_menu_state *state);
void se_menu_refresh(struct se_menu *menu, const struct se_menu_state *state);
void se_menu_close(struct se_menu *menu);

/* The window's titlebar in zdesktop (titlebar.c). */
int se_titlebar_open(struct se_titlebar *titlebar, struct se_window *window, const struct se_titlebar_state *state);
void se_titlebar_refresh(struct se_titlebar *titlebar, const struct se_titlebar_state *state);
int se_titlebar_take(struct se_titlebar *titlebar, struct se_titlebar_event *event);
void se_titlebar_close(struct se_titlebar *titlebar);

/* The window's glass (glass.c). */
int se_glass_open(struct se_glass *glass, struct se_window *window, const struct se_present *present);
void se_glass_refresh(struct se_glass *glass, struct se_app *app);
void se_glass_close(struct se_glass *glass);

/* The presenter (present.c). */
VkResult se_present_open(struct se_present *present, struct se_window *window);
VkResult se_present_resize(struct se_present *present, uint32_t width, uint32_t height);
VkResult se_present_frame(struct se_present *present, const uint32_t *pixels, size_t stride);
void se_present_close(struct se_present *present);

#endif
