/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of files that speak Wayland and Vulkan: the window
 * (window.c), the presenter of drawn frames (present.c) and the menus
 * (menu.c).  The host tests build the rest of the program without them.
 */

#ifndef KEILAND_FILES_WINDOW_H
#define KEILAND_FILES_WINDOW_H

#include "files.h"
#include "touch.h"

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <keiland.h>

/* How many inputs wait for the main loop at most. */
#define FM_WINDOW_EVENTS	256U

/* How many touch inputs wait for the main loop at most (ws081-p010). */
#define FM_WINDOW_TOUCHES	256U

/*
 * The Wayland window: its globals, its surface and roles, the size the
 * compositor gave it, and the input waiting for the interface.
 *
 * One lives for the whole run.
 */
struct fm_window {
	/* The connection and the globals bound from it. */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;

	/* The touch inputs not yet taken by the main loop, oldest first (ws081-p010; a full queue drops the newest). */
	struct fm_touch_event touches[FM_WINDOW_TOUCHES];
	unsigned touch_count;

	/*
	 * Drag and drop (dnd.c, ws035-p084): the data device manager and the
	 * seat's device (NULL without them); this window's own drag (its
	 * source, NULL for none, and the file names it offers, "text/uri-list");
	 * the offer the compositor introduced last and whether it has file
	 * names; the offer of the drag over the window (NULL for none), the
	 * serial of its enter, whether it has file names and its version.
	 */
	struct wl_data_device_manager *data_manager;
	struct wl_data_device *data_device;
	struct wl_data_source *drag_source;
	char *drag_uris;
	size_t drag_uris_length;
	char *drag_text;
	size_t drag_text_length;
	struct wl_data_offer *offer_new;
	int offer_new_files;
	struct wl_data_offer *drop_offer;
	uint32_t drop_serial;
	int drop_files;

	/* The window: its surface and roles. */
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;

	/* The desktop surface's role instead of the window's (files --desktop, ws094-p003); NULL for a window. */
	struct keiland_desktop *desktop;

	/* The size the compositor asked for, and whether it changed since it was last taken. */
	uint32_t width;
	uint32_t height;
	int resized;

	/* The largest size the window may choose for itself (xdg-shell's bounds; 0 when not known), and the size it would like. */
	uint32_t bounds_width;
	uint32_t bounds_height;
	uint32_t preferred_width;
	uint32_t preferred_height;

	/* Whether the first configure arrived, the compositor asked to close, the window has the focus, is maximized. */
	int configured;
	int closed;
	int activated;
	int maximized;

	/* The pointer's place, the serial of its last press, and the modifiers held (FM_MOD_*). */
	int pointer_x;
	int pointer_y;
	uint32_t button_serial;
	uint32_t modifiers;

	/* The key held for repeating (0 when none), when it repeats next, and the repeat's delay and interval. */
	uint32_t repeat_key;
	uint64_t repeat_at;
	uint32_t repeat_delay;
	uint32_t repeat_interval;

	/* The inputs waiting, a ring: the oldest's slot and how many. */
	struct fm_event events[FM_WINDOW_EVENTS];
	unsigned event_first;
	unsigned event_count;
};

/*
 * The window's menus as given to zdesktop (menu.c): the connection's menu
 * service (NULL when the compositor has none, and the window then has no
 * menus), the menu and the window's place for it, the state the menus last
 * showed, and the window whose inputs the choices join (FM_EVENT_ACTION).
 */
struct fm_menu {
	struct keiland_menu_service *service;
	struct keiland_menu *menu;
	struct keiland_window_menu *window_menu;
	struct fm_menu_state shown;
	struct fm_window *window;

	/*
	 * The context menu (ws071-p009): its model, made again for each right
	 * press, and the open context menu (NULL when none); done says zdesktop
	 * closed it, so that it goes at the next refresh (not inside its own
	 * event).
	 */
	struct keiland_menu *context_model;
	struct keiland_context_menu *context;
	int context_done;
};

/* How many things done with the titlebar wait for the main loop at most. */
#define FM_TITLEBAR_EVENTS	16U

/*
 * The window's titlebar as given to zdesktop (titlebar.c): zdesktop's
 * titlebar object, the state it last showed (and whether it was ever
 * sent), and what the user did with it and the main loop has not yet
 * carried out, oldest first.
 */
struct fm_titlebar {
	struct fm_window *window;
	struct keiland_titlebar *titlebar;
	struct fm_titlebar_state shown;
	int sent;
	struct fm_titlebar_event events[FM_TITLEBAR_EVENTS];
	unsigned event_count;
};

/*
 * One swapchain image the presenter draws into; the image is the
 * swapchain's.
 */
struct fm_present_target {
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
struct fm_present {
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
	struct fm_present_target *targets;
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

/* The window (window.c). */
int fm_window_open(struct fm_window *window, const char *display, uint32_t width, uint32_t height, const char *title, const char *application);
int fm_window_open_desktop(struct fm_window *window, const char *display, const char *token);
int fm_window_dispatch(struct fm_window *window, int timeout);
int fm_window_take(struct fm_window *window, struct fm_event *event);
int fm_window_repeat(struct fm_window *window, uint64_t now);
void fm_window_close(struct fm_window *window);
void fm_window_action(struct fm_window *window, uint32_t action);
void fm_window_minimize(struct fm_window *window);
void fm_window_zoom(struct fm_window *window);
struct fm_event *fm_window_push(struct fm_window *window, unsigned type);
void fm_dnd_open(struct fm_window *window);
void fm_dnd_close(struct fm_window *window);
int fm_dnd_start(struct fm_window *window, char *const *paths, size_t count);
void fm_dnd_answer(struct fm_window *window, int accept, uint32_t preferred);
int fm_dnd_receive(struct fm_window *window, char ***paths, size_t *count);
void fm_dnd_finish(struct fm_window *window, uint32_t action);
void fm_dnd_abort(struct fm_window *window);
uint64_t fm_clock(void);

/* The menus (menu.c). */
int fm_menu_open(struct fm_menu *menu, struct fm_window *window, const struct fm_menu_state *state);
void fm_menu_refresh(struct fm_menu *menu, const struct fm_menu_state *state);
void fm_menu_close(struct fm_menu *menu);
void fm_menu_context(struct fm_menu *menu, const struct fm_context *context, int x, int y, uint32_t context_serial);

/* The window's titlebar in zdesktop (titlebar.c). */
int fm_titlebar_open(struct fm_titlebar *titlebar, struct fm_window *window, const struct fm_titlebar_state *state);
void fm_titlebar_refresh(struct fm_titlebar *titlebar, const struct fm_titlebar_state *state);
int fm_titlebar_take(struct fm_titlebar *titlebar, struct fm_titlebar_event *event);
void fm_titlebar_close(struct fm_titlebar *titlebar);

/*
 * The window's glass in zdesktop (glass.c): zdesktop's glass object (NULL
 * when the window is not glass) and the panels it last sent.
 */
struct fm_glass {
	struct keiland_glass *glass;
	struct fm_panel shown[FM_PANELS];
	size_t shown_count;
	int sent;
};

/* The window's glass (glass.c). */
int fm_glass_open(struct fm_glass *glass, struct fm_window *window, const struct fm_present *present);
void fm_glass_refresh(struct fm_glass *glass, struct fm_app *app);
void fm_glass_close(struct fm_glass *glass);

/* The presenter (present.c). */
VkResult fm_present_open(struct fm_present *present, struct fm_window *window);
VkResult fm_present_resize(struct fm_present *present, uint32_t width, uint32_t height);
VkResult fm_present_frame(struct fm_present *present, const uint32_t *pixels, size_t stride);
void fm_present_close(struct fm_present *present);

#endif
