/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * DRAFT (ws074-p053): the C interface of the browser engine as a
 * component, the future libbrowser.so (plan/ws074/design.md §19).  Not
 * built and not installed; it records the shape the engine moves toward.
 * Names, fields and the split into calls may change in the phases that
 * implement it (the order is in plan/ws074/phase053/phase.md).
 *
 * The engine draws HTML5 content (a page from a URL, or an HTML string)
 * into a Vulkan image the caller owns, and takes the caller's input as
 * events.  It knows nothing of Wayland, of windows or of the program
 * around it: the caller (the /bin/browser shell, a System Settings
 * window, a widget) owns the window, the swapchain, the device, the main
 * loop and the user interface around the content.
 *
 * Threading: a view is used from one thread, the one that made it.  The
 * engine's garbage collector scans that thread's stack; the caller names
 * the outermost frame of the calls it will make (stack_base), or passes
 * NULL for the engine to take the thread's stack bounds itself.
 *
 * Time and waiting: the engine never blocks and never sleeps.  The caller
 * polls the descriptors the engine lists (the network, the resolver's
 * pipe) and calls browser_view_process when one is ready or when the
 * deadline the engine gave has passed.
 */

#ifndef KERN_BROWSER_VIEW_H
#define KERN_BROWSER_VIEW_H

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The version of this interface (the struct sizes are checked by it). */
#define BROWSER_VIEW_VERSION		1

/* A view: one browsing context (a tab's content), opaque to the caller. */
struct browser_view;

/* What the calls report: 0, or an errno value. */

/*
 * The fonts the content is drawn with (paths of TrueType files; NULL for
 * the system's defaults).
 */
struct browser_fonts {
	const char *sans;
	const char *mono;
	const char *fallback;
};

/*
 * The GPU the view draws with: the caller's instance, device and a queue
 * family that can draw (the view makes its own queue submissions on it
 * unless the caller records the drawing into its own command buffer).
 */
struct browser_gpu {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	uint32_t queue_family;
};

/* How a page load ended, for the load callback. */
enum browser_load_state {
	BROWSER_LOAD_STARTED,
	BROWSER_LOAD_COMMITTED,
	BROWSER_LOAD_FINISHED,
	BROWSER_LOAD_FAILED
};

/* What a navigation callback decides. */
enum browser_policy {
	BROWSER_POLICY_ALLOW,
	BROWSER_POLICY_DENY
};

/*
 * The caller's callbacks (any may be NULL) and the pointer they get back.
 *
 * - redraw: the content changed; draw it again when convenient (the next
 *   frame callback of the caller's surface).
 * - title: the document's title changed.
 * - location: the page's URL changed (a navigation committed, or history).
 * - history: whether back and forward are possible now.
 * - load: a load started, committed, finished or failed (reason is a text
 *   for the failure, such as a TLS verification error, else NULL).
 * - navigate: a link, a form or a script asks to open a URL; the caller
 *   may allow it (the view loads it), or deny it and open it itself (in a
 *   new tab, or an external program).
 * - console: a script wrote to the console (level 0 log, 1 warn, 2 error).
 * - cursor: the pointer shape the content wants (CSS cursor keywords).
 */
struct browser_callbacks {
	void (*redraw)(void *context, struct browser_view *view);
	void (*title)(void *context, struct browser_view *view, const char *title);
	void (*location)(void *context, struct browser_view *view, const char *url);
	void (*history)(void *context, struct browser_view *view, int can_back, int can_forward);
	void (*load)(void *context, struct browser_view *view, enum browser_load_state state, const char *reason);
	enum browser_policy (*navigate)(void *context, struct browser_view *view, const char *url, int user_initiated);
	void (*console)(void *context, struct browser_view *view, int level, const char *text, size_t length);
	void (*cursor)(void *context, struct browser_view *view, const char *cursor);
	void *context;
};

/*
 * What a view is made with: the interface version, the fonts, the GPU
 * (NULL for a view that only draws with the CPU, as the headless modes
 * and the tests do), the callbacks, the outermost stack frame of the
 * thread that uses it, and the size in pixels with the output's scale.
 */
struct browser_view_options {
	int version;
	const struct browser_fonts *fonts;
	const struct browser_gpu *gpu;
	const struct browser_callbacks *callbacks;
	const void *stack_base;
	uint32_t width;
	uint32_t height;
	float scale;
};

/* Makes and ends a view. */
int browser_view_create(const struct browser_view_options *options, struct browser_view **view);
void browser_view_destroy(struct browser_view *view);

/* Loads content: a URL (file:, data:, http:, https:, about:), or an HTML string with the URL it is from. */
int browser_view_load_url(struct browser_view *view, const char *url);
int browser_view_load_html(struct browser_view *view, const char *html, size_t length, const char *base_url);
int browser_view_reload(struct browser_view *view);
int browser_view_stop(struct browser_view *view);
int browser_view_go(struct browser_view *view, int steps);

/* The view's size in pixels and the output's scale (the layout's CSS pixels are pixels / scale). */
int browser_view_resize(struct browser_view *view, uint32_t width, uint32_t height, float scale);

/* The document's title and URL now (valid until the next call on the view). */
const char *browser_view_title(const struct browser_view *view);
const char *browser_view_url(const struct browser_view *view);

/*
 * The main loop's side: the descriptors to poll with their events, the
 * time (the caller's monotonic milliseconds) the view wants to run next
 * (a negative value for none), and the work due at a time, which may call
 * the callbacks.
 */
struct browser_poll {
	int fd;
	short events;
};
int browser_view_poll_fds(const struct browser_view *view, struct browser_poll *fds, size_t capacity, size_t *count);
int64_t browser_view_deadline(const struct browser_view *view);
int browser_view_process(struct browser_view *view, uint64_t now_ms);

/*
 * Drawing into the caller's image: the view's content at the view's size,
 * into a color image of a format, left in a layout (the view transitions
 * it from old_layout).  Either the view submits on its own (waiting for
 * wait and signalling signal, each VK_NULL_HANDLE for none), or it records
 * into the caller's command buffer, inside nothing (the view begins and
 * ends its own render pass); the caller submits and keeps the image alive
 * until the work is done.
 */
struct browser_target {
	VkImage image;
	VkImageView view;
	VkFormat format;
	VkImageLayout old_layout;
	VkImageLayout new_layout;
	uint32_t width;
	uint32_t height;
};
int browser_view_draw(struct browser_view *view, const struct browser_target *target, VkSemaphore wait, VkSemaphore signal);
int browser_view_record(struct browser_view *view, const struct browser_target *target, VkCommandBuffer commands);

/* Drawing with the CPU into 0xAARRGGBB pixels, rows stride bytes apart (the headless modes and the tests). */
int browser_view_draw_pixels(struct browser_view *view, uint32_t *pixels, uint32_t width, uint32_t height, size_t stride);

/*
 * Input, in the view's pixels from its top left.  Keys are named as the
 * DOM names them (key: "a", "Enter", "ArrowDown"; code: "KeyA"), with the
 * text they type (UTF-8, empty when none) and the modifiers held.  The
 * view does the default actions (scrolling with the wheel and the keys,
 * following a link, focusing and editing a form control) unless the
 * page's scripts cancel them.
 */
#define BROWSER_MOD_SHIFT		0x01U
#define BROWSER_MOD_CTRL		0x02U
#define BROWSER_MOD_ALT			0x04U
#define BROWSER_MOD_META		0x08U

#define BROWSER_BUTTON_PRIMARY		0
#define BROWSER_BUTTON_MIDDLE		1
#define BROWSER_BUTTON_SECONDARY	2

/* The pointer, the wheel, the keys and the focus. */
int browser_view_pointer_move(struct browser_view *view, float x, float y, uint32_t modifiers);
int browser_view_pointer_button(struct browser_view *view, float x, float y, int button, int pressed, uint32_t modifiers);
int browser_view_pointer_leave(struct browser_view *view);
int browser_view_wheel(struct browser_view *view, float x, float y, float delta_x, float delta_y, uint32_t modifiers);
int browser_view_key(struct browser_view *view, const char *key, const char *code, const char *text, int pressed, int repeat, uint32_t modifiers);
int browser_view_focus(struct browser_view *view, int focused);

/* The process-wide settings of the engine: the certificate authorities trusted besides the system's. */
int browser_add_ca_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif
