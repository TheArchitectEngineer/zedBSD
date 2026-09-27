/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the shell that speak Wayland and Vulkan: the window
 * (window.c) and the presenter that shows the GPU renderer's frames in it
 * (present.c).  The host build leaves the whole directory out.
 */

#ifndef ZDESKTOP_BROWSER_SHELL_INTERNAL_H
#define ZDESKTOP_BROWSER_SHELL_INTERNAL_H

/* The Wayland platform's parts of Vulkan, declared before anything includes vulkan.h. */
#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#include "paint/gpu.h"
#include "shell/shell.h"

/* How many inputs wait for the main loop at most. */
#define SHELL_WINDOW_EVENTS	256U

/* The kinds of input the window queues. */
enum shell_event_type {
	SHELL_EVENT_KEY,
	SHELL_EVENT_SCROLL
};

/* The modifier bits of an input. */
#define SHELL_MOD_SHIFT		0x01U
#define SHELL_MOD_CTRL		0x02U
#define SHELL_MOD_ALT		0x04U

/*
 * One input for the main loop: a key pressed (its evdev code), or a
 * scroll of some pixels (positive is down), with the modifiers held.
 */
struct shell_event {
	int type;
	uint32_t key;
	int scroll;
	uint32_t modifiers;
};

/*
 * The Wayland window: its globals, its surface and roles, the size the
 * compositor gave it, and the input waiting for the main loop.
 *
 * One lives for the whole run.
 */
struct shell_window {
	/* The connection and the globals bound from it. */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	/* The window: its surface and roles. */
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;

	/* The size the compositor asked for, and whether it changed since it was last taken. */
	uint32_t width;
	uint32_t height;
	int resized;

	/* Whether the first configure arrived and whether the compositor asked to close. */
	int configured;
	int closed;

	/* The modifiers held (SHELL_MOD_*). */
	uint32_t modifiers;

	/* The key held for repeating (0 when none), when it repeats next, and the repeat's delay and interval. */
	uint32_t repeat_key;
	uint64_t repeat_at;
	uint32_t repeat_delay;
	uint32_t repeat_interval;

	/* The inputs waiting, a ring: the oldest's slot and how many. */
	struct shell_event events[SHELL_WINDOW_EVENTS];
	unsigned event_first;
	unsigned event_count;
};

/*
 * One swapchain image the presenter draws into; the image is the
 * swapchain's.
 */
struct shell_target {
	VkImage image;
	VkImageView view;
	VkFramebuffer framebuffer;
	VkSemaphore rendered;
};

/*
 * The Vulkan objects that show the page in the window: the instance with
 * the window's surface, the device, the swapchain with a framebuffer for
 * each image, and the GPU renderer that draws into them.
 */
struct shell_present {
	/* The instance, the surface of the window, the device and its queue family. */
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;

	/* The swapchain, its format and extent, and one target per image. */
	VkSwapchainKHR swapchain;
	VkFormat format;
	VkExtent2D extent;
	struct shell_target *targets;
	uint32_t count;

	/* The semaphore the acquire signals. */
	VkSemaphore acquired;

	/* The GPU renderer on the device, and whether it was opened. */
	struct paint_gpu gpu;
	int gpu_open;

	/* The Vulkan call that failed last, for the error line. */
	const char *operation;
};

/* The window (window.c). */
int shell_window_open(struct shell_window *window, const char *display, uint32_t width, uint32_t height, const char *title);
int shell_window_dispatch(struct shell_window *window, int timeout);
int shell_window_take(struct shell_window *window, struct shell_event *event);
int shell_window_repeat(struct shell_window *window, uint64_t now);
void shell_window_title(struct shell_window *window, const char *title);
void shell_window_close(struct shell_window *window);
uint64_t shell_clock(void);

/* The presenter (present.c). */
VkResult shell_present_open(struct shell_present *present, struct shell_window *window);
VkResult shell_present_resize(struct shell_present *present, uint32_t width, uint32_t height);
VkResult shell_present_frame(struct shell_present *present, const struct paint_list *list, struct text_system *text, layout_unit scroll_y);
void shell_present_close(struct shell_present *present);

#endif
