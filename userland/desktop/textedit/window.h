/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of Text Editor that speak Wayland, Vulkan and zdesktop's
 * extensions: the window (window.c), the clipboard and the primary
 * selection (clipboard.c, primary.c), the presenter of drawn frames
 * (present.c), the menus and the context menu (menu.c), the titlebar's
 * controls and find field (titlebar.c), and the glass (glass.c).  The host
 * tests build the rest of the program without them.
 */

#ifndef TEXTEDIT_WINDOW_H
#define TEXTEDIT_WINDOW_H

#include "textedit.h"
#include "touch.h"

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <keiland.h>

/* How many inputs wait for the main loop at most. */
#define TE_WINDOW_EVENTS	256U

/* How many touch inputs wait for the main loop at most. */
#define TE_WINDOW_TOUCHES	256U

/* The primary selection's objects (primary.c includes their protocol's header). */
struct zwp_primary_selection_device_manager_v1;
struct zwp_primary_selection_device_v1;
struct zwp_primary_selection_source_v1;
struct zwp_primary_selection_offer_v1;

/*
 * The Wayland window: its globals, its surface and roles, the size the
 * compositor gave it, the input waiting for the editor, and the clipboard
 * and the primary selection.
 *
 * One lives for the whole run.
 */
struct te_window {
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

	/* The largest size the window may choose for itself (0 when not known), and the size it would like. */
	uint32_t bounds_width;
	uint32_t bounds_height;
	uint32_t preferred_width;
	uint32_t preferred_height;

	/* Whether the first configure arrived, and whether the compositor asked to close. */
	int configured;
	int closed;

	/*
	 * The pointer's place, the modifiers held (TE_MOD_*), the serial of the
	 * last input (for the selections), and the serial of the last press of
	 * a button or a finger (a context menu opens for a press).
	 */
	int pointer_x;
	int pointer_y;
	uint32_t modifiers;
	uint32_t serial;
	uint32_t press_serial;

	/* The key held for repeating (0 when none), when it repeats next, and the repeat's delay and interval. */
	uint32_t repeat_key;
	uint64_t repeat_at;
	uint32_t repeat_delay;
	uint32_t repeat_interval;

	/* The inputs waiting, a ring: the oldest's slot and how many. */
	struct te_event events[TE_WINDOW_EVENTS];
	unsigned event_first;
	unsigned event_count;

	/* The touch inputs waiting, a ring of their own: the oldest's slot and how many. */
	struct te_touch_event touches[TE_WINDOW_TOUCHES];
	unsigned touch_first;
	unsigned touch_count;

	/*
	 * The clipboard (clipboard.c): the data device manager and the seat's
	 * device, the editor's source while its text is the selection, the
	 * selection's offer and whether it has text, whether the offer being
	 * described has text, and the editor's own copied text (which the
	 * window owns).
	 */
	struct wl_data_device_manager *data_manager;
	struct wl_data_device *data_device;
	struct wl_data_source *data_source;
	struct wl_data_offer *data_offer;
	int offer_text;
	int pending_text;
	char *clipboard;
	size_t clipboard_length;

	/*
	 * The primary selection (primary.c): the manager and the seat's device,
	 * the editor's source while its selected text is the primary
	 * selection, the selection's offer and whether it has text, whether
	 * the offer being described has text, and the editor's own selected
	 * text (which the window owns).
	 */
	struct zwp_primary_selection_device_manager_v1 *primary_manager;
	struct zwp_primary_selection_device_v1 *primary_device;
	struct zwp_primary_selection_source_v1 *primary_source;
	struct zwp_primary_selection_offer_v1 *primary_offer;
	int primary_offer_text;
	int primary_pending_text;
	char *primary_text;
	size_t primary_length;
};

/*
 * One swapchain image the presenter draws into; the image is the
 * swapchain's.
 */
struct te_present_target {
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
struct te_present {
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
	struct te_present_target *targets;
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
 * What the menus and the titlebar show of the editor: whether a change can
 * be undone and redone, whether text is selected, whether the document
 * has unsaved changes, and the two View switches.
 */
struct te_state {
	int can_undo;
	int can_redo;
	int selected;
	int modified;
	int line_numbers;
	int wrap;
};

/*
 * The window's menus as given to zdesktop (menu.c): the connection's menu
 * service (NULL when the compositor has none, and the window then has no
 * menus), the window's menu and its place, the context menu's model and
 * the context menu open (NULL for none), and the state the menus last
 * showed.
 */
struct te_menu {
	struct keiland_menu_service *service;
	struct keiland_menu *menu;
	struct keiland_window_menu *window_menu;
	struct keiland_menu *context;
	struct keiland_context_menu *popup;
	struct te_state shown;
	struct te_window *window;
};

/*
 * The window's titlebar in zdesktop (titlebar.c): zdesktop's titlebar
 * object (NULL without one), the state it last showed, whether it was
 * ever sent, and whether the find field should take the keyboard.
 */
struct te_titlebar {
	struct te_window *window;
	struct keiland_titlebar *titlebar;
	struct te_state shown;
	int sent;
	int want_focus;
};

/*
 * The window's glass (glass.c): zdesktop's glass object (NULL when the
 * window keeps its own ground), and the card it was last told of.
 */
struct te_glass {
	struct keiland_glass *glass;
	struct te_rect shown;
	int sent;
};

/* The window (window.c). */
int te_window_open(struct te_window *window, const char *display, uint32_t width, uint32_t height, const char *title, const char *application);
int te_window_dispatch(struct te_window *window, int timeout);
int te_window_take(struct te_window *window, struct te_event *event);
int te_window_take_touch(struct te_window *window, struct te_touch_event *event);
int te_window_repeat(struct te_window *window, uint64_t now);
struct te_event *te_window_push(struct te_window *window, enum te_event_type type);
void te_window_action(struct te_window *window, uint32_t action);
void te_window_title(struct te_window *window, const char *title);
void te_window_close(struct te_window *window);

/* The clipboard (clipboard.c). */
void te_clipboard_bind(struct te_window *window, struct wl_registry *registry, uint32_t name, uint32_t version);
void te_clipboard_start(struct te_window *window);
void te_clipboard_set(struct te_window *window, const char *text, size_t length);
size_t te_clipboard_receive(struct te_window *window, char *text, size_t size);
void te_clipboard_close(struct te_window *window);

/* The primary selection (primary.c). */
void te_primary_bind(struct te_window *window, struct wl_registry *registry, uint32_t name);
void te_primary_start(struct te_window *window);
void te_primary_set(struct te_window *window, const char *text, size_t length);
size_t te_primary_receive(struct te_window *window, char *text, size_t size);
void te_primary_close(struct te_window *window);

/* The menus (menu.c). */
int te_menu_open(struct te_menu *menu, struct te_window *window, const struct te_state *state);
void te_menu_refresh(struct te_menu *menu, const struct te_state *state);
void te_menu_popup(struct te_menu *menu, int x, int y);
void te_menu_close(struct te_menu *menu);

/* The titlebar's controls (titlebar.c). */
int te_titlebar_open(struct te_titlebar *titlebar, struct te_window *window, const struct te_state *state);
void te_titlebar_refresh(struct te_titlebar *titlebar, const struct te_state *state);
void te_titlebar_close(struct te_titlebar *titlebar);

/* The glass (glass.c). */
int te_glass_open(struct te_glass *glass, struct te_window *window, const struct te_present *present);
void te_glass_refresh(struct te_glass *glass, const struct te_app *app);
void te_glass_close(struct te_glass *glass);

/* The presenter (present.c). */
VkResult te_present_open(struct te_present *present, struct te_window *window);
VkResult te_present_resize(struct te_present *present, uint32_t width, uint32_t height);
VkResult te_present_frame(struct te_present *present, const uint32_t *pixels, size_t stride);
void te_present_close(struct te_present *present);

#endif
