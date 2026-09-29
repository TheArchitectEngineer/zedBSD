/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of Image Viewer that speak Wayland, Vulkan and zdesktop's
 * extensions: the window (window.c), the presenter of the image and the
 * drawn canvas (present.c), the menus (menu.c), the titlebar's controls
 * (titlebar.c) and the window's glass (glass.c).  The host tests build the
 * rest of the program without them.
 */

#ifndef IMAGEVIEW_WINDOW_H
#define IMAGEVIEW_WINDOW_H

#include "imageview.h"
#include "touch.h"

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <keiland.h>

/* How many inputs wait for the main loop at most. */
#define IV_WINDOW_EVENTS	256U

/* How many touch inputs wait for the main loop at most (ws081-p012). */
#define IV_WINDOW_TOUCHES	256U

/*
 * The Wayland window: its globals, its surface and roles, the size the
 * compositor gave it, and the input waiting for the viewer.
 *
 * One lives for the whole run.
 */
struct iv_window {
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

	/* Whether the compositor last configured the window fullscreen. */
	int fullscreen;

	/* The serial of the last press (a button or a finger), which a context menu is opened with. */
	uint32_t press_serial;

	/* The pointer's place and the modifiers held (IV_MOD_*). */
	int pointer_x;
	int pointer_y;
	uint32_t modifiers;

	/* The key held for repeating (0 when none), when it repeats next, and the repeat's delay and interval. */
	uint32_t repeat_key;
	uint64_t repeat_at;
	uint32_t repeat_delay;
	uint32_t repeat_interval;

	/* The inputs waiting, a ring: the oldest's slot and how many. */
	struct iv_event events[IV_WINDOW_EVENTS];
	unsigned event_first;
	unsigned event_count;

	/* The touch inputs waiting, a ring of their own (ws081-p012): the oldest's slot and how many. */
	struct iv_touch_event touches[IV_WINDOW_TOUCHES];
	unsigned touch_first;
	unsigned touch_count;
};

/*
 * One swapchain image the presenter draws into; the image is the
 * swapchain's.
 */
struct iv_present_target {
	VkImage image;
	VkImageView view;
	VkFramebuffer framebuffer;
	VkSemaphore rendered;
};

/*
 * One level of the image as a texture: a linear, host-written image with
 * its memory (mapped for good), view, and the sets that bind it smoothly
 * and to the nearest texel; its size, and whether it left its
 * preinitialized layout.
 */
struct iv_present_level {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkDescriptorSet smooth_set;
	VkDescriptorSet nearest_set;
	unsigned char *map;
	size_t pitch;
	int width;
	int height;
	int ready;
};

/*
 * The Vulkan objects that show the frames in the window: a swapchain; the
 * image's levels as textures, one of which a quad draws where the view
 * places it; and a host-written canvas image the size of the window that
 * a second quad lays over it by its premultiplied alpha.
 */
struct iv_present {
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
	struct iv_present_target *targets;
	uint32_t count;

	/* The pass and the pipeline that draw the canvas, and what the pipeline binds. */
	VkRenderPass pass;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet set;
	VkSampler sampler;
	VkSampler smooth_sampler;

	/* The image's levels, the image they are of (image_serial), and whether they can be sampled smoothly and how large one may be. */
	struct iv_present_level levels[IV_LEVELS_MAX];
	size_t level_count;
	unsigned image_serial;
	int has_image;
	int smooth;
	int max_dimension;

	/* The canvas image (linear, host-written), its memory, view and map, and whether it was made ready. */
	VkImage canvas;
	VkDeviceMemory canvas_memory;
	VkImageView canvas_view;
	unsigned char *canvas_map;
	size_t canvas_pitch;
	int canvas_ready;

	/* The two quads' vertices (the canvas's, then the image's), in host-visible memory mapped for good. */
	VkBuffer vertices;
	VkDeviceMemory vertex_memory;
	float *vertex_map;

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
 * What the menus and the titlebar show of the viewer: whether an image is
 * shown (and can be), its place and the folder's count, whether it is
 * fitted, animated (and playing) and fullscreen.
 */
struct iv_state {
	int has_image;
	int can_show;
	size_t index;
	size_t count;
	int fit;
	int animated;
	int playing;
	int fullscreen;
};

/*
 * The window's glass in zdesktop (glass.c): the object (NULL without
 * glass), and the panels last sent.
 */
struct iv_glass {
	struct keiland_glass *glass;
	struct keiland_glass_panel panels[2];
	size_t count;
	int sent;
};

/*
 * The window's menus as given to zdesktop (menu.c): the connection's menu
 * service (NULL when the compositor has none, and the window then has no
 * menus), the menu and the window's place for it, and the state the menus
 * last showed.
 */
struct iv_menu {
	struct keiland_menu_service *service;
	struct keiland_menu *menu;
	struct keiland_window_menu *window_menu;
	struct iv_state shown;
	struct iv_window *window;
	struct keiland_menu *context;
	struct keiland_context_menu *popup;
};

/*
 * The window's titlebar in zdesktop (titlebar.c): zdesktop's titlebar
 * object (NULL without one), the state it last showed, and whether it was
 * ever sent.
 */
struct iv_titlebar {
	struct iv_window *window;
	struct keiland_titlebar *titlebar;
	struct iv_state shown;
	int sent;
};

/* The window (window.c). */
int iv_window_open(struct iv_window *window, const char *display, uint32_t width, uint32_t height, const char *title, const char *application);
int iv_window_dispatch(struct iv_window *window, int timeout);
int iv_window_take(struct iv_window *window, struct iv_event *event);
int iv_window_take_touch(struct iv_window *window, struct iv_touch_event *event);
int iv_window_repeat(struct iv_window *window, uint64_t now);
int iv_window_repeat_wait(const struct iv_window *window, uint64_t now);
void iv_window_action(struct iv_window *window, uint32_t action);
void iv_window_title(struct iv_window *window, const char *title);
void iv_window_fullscreen(struct iv_window *window, int fullscreen);
void iv_window_close(struct iv_window *window);

/* The menus (menu.c). */
int iv_menu_open(struct iv_menu *menu, struct iv_window *window, const struct iv_state *state);
void iv_menu_refresh(struct iv_menu *menu, const struct iv_state *state);
void iv_menu_close(struct iv_menu *menu);
void iv_menu_context(struct iv_menu *menu, const struct iv_state *state, int x, int y);

/* The titlebar's controls (titlebar.c). */
int iv_titlebar_open(struct iv_titlebar *titlebar, struct iv_window *window, const struct iv_state *state);
void iv_titlebar_refresh(struct iv_titlebar *titlebar, const struct iv_state *state);
void iv_titlebar_close(struct iv_titlebar *titlebar);

/* The presenter (present.c). */
VkResult iv_present_open(struct iv_present *present, struct iv_window *window);
VkResult iv_present_resize(struct iv_present *present, uint32_t width, uint32_t height);
VkResult iv_present_frame(struct iv_present *present, const uint32_t *pixels, size_t stride, int canvas_changed, const struct iv_quad *quad, uint32_t ground);
VkResult iv_present_set_image(struct iv_present *present, const struct iv_image *image, unsigned serial);
void iv_present_set_frame(struct iv_present *present, const uint32_t *pixels);
void iv_present_close(struct iv_present *present);

/* The glass (glass.c). */
int iv_glass_open(struct iv_glass *glass, struct iv_window *window, const struct iv_present *present);
void iv_glass_update(struct iv_glass *glass, const struct iv_app *app);
void iv_glass_close(struct iv_glass *glass);

#endif
