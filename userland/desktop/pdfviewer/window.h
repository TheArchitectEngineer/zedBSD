/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of PDF Viewer that speak Wayland, Vulkan and zdesktop's
 * extensions: the window (window.c), the presenter of drawn frames
 * (present.c), the menus (menu.c) and the titlebar's controls
 * (titlebar.c).  The host tests build the rest of the program without them.
 */

#ifndef PDFVIEWER_WINDOW_H
#define PDFVIEWER_WINDOW_H

#include "viewer.h"
#include "touch.h"

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <keiland.h>

/* How many inputs wait for the main loop at most. */
#define PV_WINDOW_EVENTS	256U

/* How many touch inputs wait for the main loop at most (ws081-p012). */
#define PV_WINDOW_TOUCHES	256U

/*
 * The Wayland window: its globals, its surface and roles, the size the
 * compositor gave it, and the input waiting for the viewer.
 *
 * One lives for the whole run.
 */
struct pv_window {
	/* The connection and the globals bound from it. */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;

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

	/* Whether the first configure arrived, and whether the compositor asked to close. */
	int configured;
	int closed;

	/* The pointer's place and the modifiers held (PV_MOD_*). */
	int pointer_x;
	int pointer_y;
	uint32_t modifiers;

	/* The key held for repeating (0 when none), when it repeats next, and the repeat's delay and interval. */
	uint32_t repeat_key;
	uint64_t repeat_at;
	uint32_t repeat_delay;
	uint32_t repeat_interval;

	/* The inputs waiting, a ring: the oldest's slot and how many. */
	struct pv_event events[PV_WINDOW_EVENTS];
	unsigned event_first;
	unsigned event_count;

	/* The touch inputs waiting, a ring of their own (ws081-p012): the oldest's slot and how many. */
	struct pv_touch_event touches[PV_WINDOW_TOUCHES];
	unsigned touch_first;
	unsigned touch_count;
};

/*
 * One swapchain image the presenter draws into; the image is the
 * swapchain's.
 */
struct pv_present_target {
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
struct pv_present {
	/* The instance, the surface of the window, the device and its queue. */
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;

	/* The swapchain, its format and extent, and one target per image. */
	VkSwapchainKHR swapchain;
	VkFormat format;
	/* Whether the swapchain is see-through: zdesktop blends the frame by its premultiplied alpha. */
	int premultiplied;
	VkExtent2D extent;
	struct pv_present_target *targets;
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
 * What the menus and the titlebar show of the viewer: whether a document
 * is open, the page and the count, the mode and the fit.
 */
struct pv_state {
	int has_document;
	size_t page;
	size_t count;
	int mode;
	int fit;
	int thumbnails;
};

/*
 * The window's menus as given to zdesktop (menu.c): the connection's menu
 * service (NULL when the compositor has none, and the window then has no
 * menus), the menu and the window's place for it, and the state the menus
 * last showed.
 */
struct pv_menu {
	struct keiland_menu_service *service;
	struct keiland_menu *menu;
	struct keiland_window_menu *window_menu;
	struct pv_state shown;
	struct pv_window *window;
};

/*
 * The window's titlebar in zdesktop (titlebar.c): zdesktop's titlebar
 * object (NULL without one), the state it last showed, and whether it was
 * ever sent.
 */
struct pv_titlebar {
	struct pv_window *window;
	struct keiland_titlebar *titlebar;
	struct pv_state shown;
	int sent;
};

/* The window (window.c). */
int pv_window_open(struct pv_window *window, const char *display, uint32_t width, uint32_t height, const char *title, const char *application);
int pv_window_dispatch(struct pv_window *window, int timeout);
int pv_window_take(struct pv_window *window, struct pv_event *event);
int pv_window_take_touch(struct pv_window *window, struct pv_touch_event *event);
int pv_window_repeat(struct pv_window *window, uint64_t now);
void pv_window_action(struct pv_window *window, uint32_t action);
void pv_window_title(struct pv_window *window, const char *title);
void pv_window_close(struct pv_window *window);

/* The menus (menu.c). */
int pv_menu_open(struct pv_menu *menu, struct pv_window *window, const struct pv_state *state);
void pv_menu_refresh(struct pv_menu *menu, const struct pv_state *state);
void pv_menu_close(struct pv_menu *menu);

/* The titlebar's controls (titlebar.c). */
int pv_titlebar_open(struct pv_titlebar *titlebar, struct pv_window *window, const struct pv_state *state);
void pv_titlebar_refresh(struct pv_titlebar *titlebar, const struct pv_state *state);
void pv_titlebar_close(struct pv_titlebar *titlebar);

/* The presenter (present.c). */
VkResult pv_present_open(struct pv_present *present, struct pv_window *window);
VkResult pv_present_resize(struct pv_present *present, uint32_t width, uint32_t height);
VkResult pv_present_frame(struct pv_present *present, const uint32_t *pixels, size_t stride);
void pv_present_close(struct pv_present *present);

#endif
