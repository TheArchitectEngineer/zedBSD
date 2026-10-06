/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws131-p018: the host test of a window's declarative tabs
 * (kl_window_set_tabs in libkeiland's window-declare.c, KL_VERSION 44).
 * The titlebar's calls are recorded by stubs; the checks follow what a
 * table of tabs sends (the titlebar made in the tabs mode, tabs added,
 * set and removed, only a change sent, the titlebar taken away with the
 * last tab, the controls' mode again beside controls) and the inputs the
 * titlebar's events become (KL_WINDOW_TAB).
 */

#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>

#include "window.h"

#include <keiland.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void record(const char *format, ...);
static void expect(const char *what, const char *calls);
static void check(int ok, const char *what);

/* The calls recorded since the last check, one a line. */
static char calls[4096];

/* How many checks failed. */
static int failures;

/* The titlebar the stubs made (an address of their own), its listener and its data. */
static int titlebar_object;
static const struct kl_titlebar_listener *titlebar_listener;
static void *titlebar_data;

/* The inputs the window queued. */
static struct kl_window_event pushed[8];
static unsigned pushed_count;

/*
 * Runs the checks.
 */
int
main(void)
{
	static const struct kl_tab_entry two[2] = {
		{ 1U, "one", KL_TAB_CLOSABLE },
		{ 2U, "two", KL_TAB_CLOSABLE | KL_TAB_ACTIVE }
	};
	static const struct kl_tab_entry renamed[1] = {
		{ 2U, "second", KL_TAB_ACTIVE }
	};
	static const struct kl_control_entry controls[1] = {
		{ 7U, KL_CONTROL_HOME, KL_PRIORITY_NORMAL, 0U, "Open", 40U }
	};
	struct kl_window *window;
	int error;

	/* A window with nothing shown. */
	window = calloc(1, sizeof(*window));
	if (window == NULL)
		return 1;
	keiui_declare_window_init(window);

	/* A toplevel's window (the stubs never use the object; a desktop surface has no titlebar). */
	window->toplevel = (struct xdg_toplevel *)&titlebar_object;

	/* Two tabs: the titlebar in the tabs mode, both added and set, the options, the mode, one commit. */
	error = kl_window_set_tabs(window, two, 2U, KL_TABS_NEW_BUTTON);
	check(error == 0, "two tabs are shown");
	expect("two tabs", "create\nbegin\nmode 2\ncommit\nbegin\nadd 1 one\nset 1 one 4\nadd 2 two\nset 2 two 5\noptions 1\nmode 2\ncommit\n");

	/* The same table again: nothing is sent. */
	error = kl_window_set_tabs(window, two, 2U, KL_TABS_NEW_BUTTON);
	check(error == 0, "the same tabs again");
	expect("the same tabs", "");

	/* One tab renamed, the other gone. */
	error = kl_window_set_tabs(window, renamed, 1U, KL_TABS_NEW_BUTTON);
	check(error == 0, "one tab renamed, one gone");
	expect("a change", "begin\nremove 1\nset 2 second 1\noptions 1\nmode 2\ncommit\n");

	/* The titlebar's events are the window's KL_WINDOW_TAB inputs. */
	titlebar_listener->tab_activated(titlebar_data, (struct kl_titlebar *)&titlebar_object, 2U, 77U);
	titlebar_listener->tab_close_requested(titlebar_data, (struct kl_titlebar *)&titlebar_object, 2U);
	titlebar_listener->new_tab_requested(titlebar_data, (struct kl_titlebar *)&titlebar_object, 78U);
	check(pushed_count == 3U, "three inputs");
	check(pushed[0].kind == KL_WINDOW_TAB && pushed[0].code == KL_WINDOW_TAB_CHOSEN && pushed[0].id == 2, "a tab chosen");
	check(pushed[1].code == KL_WINDOW_TAB_CLOSE && pushed[1].id == 2, "a tab's close button");
	check(pushed[2].code == KL_WINDOW_TAB_NEW && pushed[2].id == 0, "the new tab's button");
	check(window->serial == 78U, "a choice is the window's last input");

	/* No tabs and no controls: the titlebar goes (the window shows its menu). */
	error = kl_window_set_tabs(window, NULL, 0U, KL_TABS_NEW_BUTTON);
	check(error == 0 && window->titlebar == NULL, "no tabs: no titlebar");
	expect("no tabs", "destroy\n");

	/* Controls, then tabs beside them, then no tabs: the controls' mode again, the titlebar kept. */
	error = kl_window_set_controls(window, controls, 1U);
	check(error == 0, "a control is shown");
	calls[0] = '\0';
	error = kl_window_set_tabs(window, two, 2U, 0U);
	check(error == 0, "tabs beside the control");
	expect("tabs beside the control", "begin\nadd 1 one\nset 1 one 4\nadd 2 two\nset 2 two 5\noptions 0\nmode 2\ncommit\n");
	error = kl_window_set_tabs(window, NULL, 0U, 0U);
	check(error == 0 && window->titlebar != NULL, "no tabs beside the control: the titlebar stays");
	expect("the controls again", "begin\nremove 1\nremove 2\noptions 0\nmode 1\ncommit\n");

	/* A tab without an ID is refused. */
	error = kl_window_set_tabs(window, (const struct kl_tab_entry[1]){ { 0U, "none", 0U } }, 1U, 0U);
	check(error == EINVAL, "a tab without an ID is refused");

	/* The outcome. */
	keiui_declare_window_close(window);
	free(window);
	if (failures != 0) {
		printf("host-tabs: FAIL (%d)\n", failures);
		return 1;
	}
	printf("host-tabs: PASS\n");
	return 0;
}

/* Adds a line to the calls recorded. */
static void
record(
	const char *format,
	...)
{
	va_list arguments;
	size_t used;

	/* After what is there, and a newline. */
	used = strlen(calls);
	va_start(arguments, format);
	(void)vsnprintf(calls + used, sizeof(calls) - used, format, arguments);
	va_end(arguments);
	used = strlen(calls);
	if (used + 1U < sizeof(calls)) {
		calls[used] = '\n';
		calls[used + 1U] = '\0';
	}
}

/* Checks the calls recorded since the last check, and starts again. */
static void
expect(
	const char *what,
	const char *expected)
{
	int same;

	/* The calls, as expected. */
	same = strcmp(calls, expected);
	check(same == 0, what);
	if (same != 0)
		printf("  got:\n%s  expected:\n%s", calls, expected);
	calls[0] = '\0';
}

/* Reports one check. */
static void
check(
	int ok,
	const char *what)
{
	/* The line, and the count of failures. */
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* The stubs of the titlebar, recording their calls. */
struct kl_titlebar *
kl_titlebar_create(struct wl_display *display, struct xdg_toplevel *toplevel, const struct kl_titlebar_listener *listener, void *data)
{
	(void)display;
	(void)toplevel;
	titlebar_listener = listener;
	titlebar_data = data;
	record("create");
	return (struct kl_titlebar *)&titlebar_object;
}

void
kl_titlebar_destroy(struct kl_titlebar *titlebar)
{
	if (titlebar != NULL)
		record("destroy");
}

int
kl_titlebar_begin(struct kl_titlebar *titlebar)
{
	(void)titlebar;
	record("begin");
	return 0;
}

int
kl_titlebar_commit(struct kl_titlebar *titlebar)
{
	(void)titlebar;
	record("commit");
	return 0;
}

int
kl_titlebar_set_mode(struct kl_titlebar *titlebar, unsigned mode)
{
	(void)titlebar;
	record("mode %u", mode);
	return 0;
}

int
kl_titlebar_add_tab(struct kl_titlebar *titlebar, uint32_t id, const char *title)
{
	(void)titlebar;
	record("add %u %s", id, title);
	return 0;
}

int
kl_titlebar_remove_tab(struct kl_titlebar *titlebar, uint32_t id)
{
	(void)titlebar;
	record("remove %u", id);
	return 0;
}

int
kl_titlebar_set_tab(struct kl_titlebar *titlebar, uint32_t id, const char *title, unsigned flags)
{
	(void)titlebar;
	record("set %u %s %u", id, title, flags);
	return 0;
}

int
kl_titlebar_set_tabs_options(struct kl_titlebar *titlebar, unsigned options)
{
	(void)titlebar;
	record("options %u", options);
	return 0;
}

int
kl_titlebar_add_control(struct kl_titlebar *titlebar, uint32_t id, unsigned role, unsigned priority, unsigned group, const char *label)
{
	(void)titlebar;
	(void)role;
	(void)priority;
	(void)group;
	record("control %u %s", id, label);
	return 0;
}

int
kl_titlebar_remove_control(struct kl_titlebar *titlebar, uint32_t id)
{
	(void)titlebar;
	(void)id;
	return 0;
}

int
kl_titlebar_set_control_label(struct kl_titlebar *titlebar, uint32_t id, const char *label)
{
	(void)titlebar;
	(void)id;
	(void)label;
	return 0;
}

int
kl_titlebar_set_control_state(struct kl_titlebar *titlebar, uint32_t id, int enabled, int checked)
{
	(void)titlebar;
	(void)id;
	(void)enabled;
	(void)checked;
	return 0;
}

int
kl_titlebar_set_control_text(struct kl_titlebar *titlebar, uint32_t id, const char *text, const char *placeholder)
{
	(void)titlebar;
	(void)id;
	(void)text;
	(void)placeholder;
	return 0;
}

int
kl_titlebar_focus_control(struct kl_titlebar *titlebar, uint32_t id, unsigned mode)
{
	(void)titlebar;
	(void)id;
	(void)mode;
	return 0;
}

/* The window's queue: the inputs kept for the checks. */
struct kl_window_event *
keiui_window_push(struct kl_window *window, unsigned kind)
{
	struct kl_window_event *event;

	if (pushed_count == sizeof(pushed) / sizeof(pushed[0]))
		return NULL;
	event = &pushed[pushed_count++];
	memset(event, 0, sizeof(*event));
	event->kind = kind;
	event->serial = window->serial;
	return event;
}

/* The parts the test does not reach. */
struct kl_menu_service *keiui_app_menu_service(struct kl_app *app) { (void)app; return NULL; }
struct kl_menu_service *kl_menu_service_open(struct wl_display *display) { (void)display; return NULL; }
void kl_menu_service_close(struct kl_menu_service *service) { (void)service; }
struct kl_menu *kl_menu_create(struct kl_menu_service *service) { (void)service; return NULL; }
void kl_menu_destroy(struct kl_menu *menu) { (void)menu; }
int kl_menu_begin(struct kl_menu *menu) { (void)menu; return 0; }
int kl_menu_commit(struct kl_menu *menu) { (void)menu; return 0; }
int kl_menu_append(struct kl_menu *menu, uint32_t id, uint32_t parent, unsigned type, const char *label, uint32_t action) { (void)menu; (void)id; (void)parent; (void)type; (void)label; (void)action; return 0; }
int kl_menu_remove(struct kl_menu *menu, uint32_t id) { (void)menu; (void)id; return 0; }
int kl_menu_set_action(struct kl_menu *menu, uint32_t id, uint32_t action) { (void)menu; (void)id; (void)action; return 0; }
int kl_menu_set_checked(struct kl_menu *menu, uint32_t id, int checked) { (void)menu; (void)id; (void)checked; return 0; }
int kl_menu_set_enabled(struct kl_menu *menu, uint32_t id, int enabled) { (void)menu; (void)id; (void)enabled; return 0; }
int kl_menu_set_label(struct kl_menu *menu, uint32_t id, const char *label) { (void)menu; (void)id; (void)label; return 0; }
int kl_menu_set_role(struct kl_menu *menu, uint32_t id, unsigned role) { (void)menu; (void)id; (void)role; return 0; }
int kl_menu_set_shortcut(struct kl_menu *menu, uint32_t id, unsigned modifiers, uint32_t keysym) { (void)menu; (void)id; (void)modifiers; (void)keysym; return 0; }
int kl_menu_set_visible(struct kl_menu *menu, uint32_t id, int visible) { (void)menu; (void)id; (void)visible; return 0; }
struct kl_context_menu *kl_menu_popup(struct kl_menu_service *service, struct kl_menu *menu, struct wl_surface *surface, int32_t x, int32_t y, struct wl_seat *seat, uint32_t serial, const struct kl_context_menu_listener *listener, void *data) { (void)service; (void)menu; (void)surface; (void)seat; (void)serial; (void)x; (void)y; (void)listener; (void)data; return NULL; }
void kl_context_menu_destroy(struct kl_context_menu *context_menu) { (void)context_menu; }
struct kl_window_menu *kl_window_menu_create(struct kl_menu_service *service, struct xdg_toplevel *toplevel, const struct kl_window_menu_listener *listener, void *data) { (void)service; (void)toplevel; (void)listener; (void)data; return NULL; }
void kl_window_menu_destroy(struct kl_window_menu *window_menu) { (void)window_menu; }
int kl_window_menu_set(struct kl_window_menu *window_menu, struct kl_menu *menu) { (void)window_menu; (void)menu; return 0; }
struct kl_glass *kl_glass_create(struct wl_display *display, struct wl_surface *surface) { (void)display; (void)surface; return NULL; }
void kl_glass_destroy(struct kl_glass *glass) { (void)glass; }
int kl_glass_set_blur(struct kl_glass *glass, int enabled) { (void)glass; (void)enabled; return 0; }
int kl_titlebar_set_control_value(struct kl_titlebar *titlebar, uint32_t id, unsigned value) { (void)titlebar; (void)id; (void)value; return 0; }
int kl_titlebar_set_suggestions(struct kl_titlebar *titlebar, uint32_t id, const char *const *labels, const char *const *texts, size_t count) { (void)titlebar; (void)id; (void)labels; (void)texts; (void)count; return 0; }
int kl_titlebar_set_breadcrumb(struct kl_titlebar *titlebar, uint32_t id, const char *const *segments, size_t count) { (void)titlebar; (void)id; (void)segments; (void)count; return 0; }
int kl_glass_set_panels(struct kl_glass *glass, const struct kl_glass_panel *panels, size_t count) { (void)glass; (void)panels; (void)count; return 0; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR *info, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface) { (void)instance; (void)info; (void)allocator; (void)surface; return VK_ERROR_INITIALIZATION_FAILED; }
