/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The declarative menus, controls and glass of a window (WS131 p015,
 * KL_VERSION 26): the tables the application gives are compared with the
 * ones shown (declare.c), and the changes go to libkeiland's menu (the
 * System Menu and the context menus), the Titlebar Presentation and the
 * glass panels.  An item or a control chosen is queued among the window's
 * inputs as a KL_WINDOW_ACTION input, so that it keeps its place among the
 * keys.  A window of an application uses the application's menu service;
 * any other window opens one of its own when it first needs one.
 *
 * Also the Vulkan surface over a window an application draws with its own
 * Vulkan instance.
 */

/* The Vulkan header first, so that <keiland-ui.h> declares the Vulkan surface. */
#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>

#include "window.h"

#include <keiland.h>

#include <errno.h>
#include <string.h>

static struct kl_menu_service *declare_service(struct kl_window *window);
static int declare_menu_sink(void *data, unsigned operation, const struct keiui_declare_item *item);
static int declare_control_sink(void *data, unsigned operation, const struct keiui_declare_item *item);
static int declare_titlebar(struct kl_window *window);
static void declare_action(struct kl_window *window, uint32_t action, uint32_t id, uint32_t detail);
static void declare_menu_activated(void *data, struct kl_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
static void declare_popup_activated(void *data, struct kl_context_menu *context_menu, uint32_t item, uint32_t action, uint32_t serial);
static void declare_popup_done(void *data, struct kl_context_menu *context_menu);
static void declare_control_activated(void *data, struct kl_titlebar *titlebar, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void declare_text_changed(void *data, struct kl_titlebar *titlebar, uint32_t id, const char *text);
static void declare_text_done(void *data, struct kl_titlebar *titlebar, uint32_t id, const char *text, unsigned how);
static void declare_text(struct kl_window *window, unsigned kind, uint32_t id, const char *text, unsigned how);

/* What the window menu tells the window: only the choices. */
static const struct kl_window_menu_listener declare_menu_listener = {
	declare_menu_activated,
	NULL,
	NULL
};

/* What a context menu tells the window: the choice, and that it closed. */
static const struct kl_context_menu_listener declare_popup_listener = {
	declare_popup_activated,
	declare_popup_done
};

/* What the titlebar tells the window: the controls chosen, and a field's text (KL_VERSION 43). */
static const struct kl_titlebar_listener declare_titlebar_listener = {
	declare_control_activated,
	declare_text_changed,
	declare_text_done,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

/*
 * Makes a window's declarative models (nothing shown yet).
 */
void
keiui_declare_window_init(
	struct kl_window *window)
{
	/* The action states, and the three models. */
	keiui_declare_states_init(&window->action_states);
	keiui_declare_init(&window->menu_model, KEIUI_DECLARE_MENU);
	keiui_declare_init(&window->control_model, KEIUI_DECLARE_CONTROLS);
	keiui_declare_init(&window->popup_model, KEIUI_DECLARE_MENU);
}

/*
 * Takes a window's menus, controls and glass away, before its toplevel and
 * surface go.
 */
void
keiui_declare_window_close(
	struct kl_window *window)
{
	/* A context menu, its menu, the window's place for a menu and its menu. */
	if (window->popup != NULL)
		kl_context_menu_destroy(window->popup);
	if (window->popup_menu != NULL)
		kl_menu_destroy(window->popup_menu);
	if (window->window_menu != NULL)
		kl_window_menu_destroy(window->window_menu);
	if (window->menu != NULL)
		kl_menu_destroy(window->menu);
	window->popup = NULL;
	window->popup_menu = NULL;
	window->window_menu = NULL;
	window->menu = NULL;

	/* The window's own menu service (an application's stays the application's). */
	if (window->menu_service != NULL)
		kl_menu_service_close(window->menu_service);
	window->menu_service = NULL;

	/* The titlebar and the glass. */
	kl_titlebar_destroy(window->titlebar);
	kl_glass_destroy(window->glass);
	window->titlebar = NULL;
	window->glass = NULL;

	/* The models. */
	keiui_declare_fini(&window->menu_model);
	keiui_declare_fini(&window->control_model);
	keiui_declare_fini(&window->popup_model);
}

/*
 * Shows a table of menu items as the window's menu (count 0 takes it away).
 */
int
kl_window_set_menu(
	struct kl_window *window,
	const struct kl_menu_entry *entries,
	size_t count)
{
	struct kl_menu_service *service;
	int error;

	/* A window and a table. */
	if (window == NULL || (entries == NULL && count != 0U))
		return EINVAL;

	/* No menu: the window's place shows none, and nothing is kept. */
	if (count == 0U) {
		if (window->window_menu != NULL)
			(void)kl_window_menu_set(window->window_menu, NULL);
		if (window->menu != NULL)
			kl_menu_destroy(window->menu);
		window->menu = NULL;
		keiui_declare_fini(&window->menu_model);
		return 0;
	}

	/* The menu service; a compositor without the System Menu shows none. */
	service = declare_service(window);
	if (service == NULL)
		return ENOTSUP;

	/* The window's place for a menu, made once. */
	if (window->window_menu == NULL) {
		window->window_menu = kl_window_menu_create(service, window->toplevel, &declare_menu_listener, window);
		if (window->window_menu == NULL)
			return errno;
	}

	/* The menu, made empty when there is none (and shown on the window). */
	if (window->menu == NULL) {
		window->menu = kl_menu_create(service);
		if (window->menu == NULL)
			return errno;
		keiui_declare_fini(&window->menu_model);
		error = kl_window_menu_set(window->window_menu, window->menu);
		if (error != 0)
			return error;
	}

	/* What changed from the table shown. */
	error = keiui_declare_menu(&window->menu_model, entries, count, &window->action_states, declare_menu_sink, window->menu);
	return error;
}

/*
 * Shows a table of controls in the window's titlebar (count 0 gives the
 * titlebar back to the menu).
 */
int
kl_window_set_controls(
	struct kl_window *window,
	const struct kl_control_entry *entries,
	size_t count)
{
	int error;

	/* A window and a table. */
	if (window == NULL || (entries == NULL && count != 0U))
		return EINVAL;

	/* No controls: the titlebar goes, and nothing is kept. */
	if (count == 0U) {
		kl_titlebar_destroy(window->titlebar);
		window->titlebar = NULL;
		keiui_declare_fini(&window->control_model);
		return 0;
	}

	/* The titlebar in the controls mode, made once. */
	error = declare_titlebar(window);
	if (error != 0)
		return error;

	/* What changed from the table shown. */
	error = keiui_declare_controls(&window->control_model, entries, count, &window->action_states, declare_control_sink, window->titlebar);
	return error;
}

/*
 * Sets a control's text and placeholder in one transaction of the
 * titlebar (KL_VERSION 43).  Returns 0, EINVAL, ENOTSUP without the
 * titlebar, or the titlebar's refusal.
 */
int
kl_window_set_control_text(
	struct kl_window *window,
	uint32_t id,
	const char *text,
	const char *placeholder)
{
	int error;

	/* A window whose controls are shown. */
	if (window == NULL)
		return EINVAL;
	if (window->titlebar == NULL)
		return ENOTSUP;

	/* The text and the placeholder, together. */
	error = kl_titlebar_begin(window->titlebar);
	if (error != 0)
		return error;
	error = kl_titlebar_set_control_text(window->titlebar, id, text, placeholder);
	if (error != 0) {
		(void)kl_titlebar_commit(window->titlebar);
		return error;
	}

	/* Shown. */
	error = kl_titlebar_commit(window->titlebar);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Gives a control's field the keyboard (KL_VERSION 43).  Returns 0,
 * EINVAL, ENOTSUP without the titlebar, or the titlebar's refusal.
 */
int
kl_window_focus_control(
	struct kl_window *window,
	uint32_t id)
{
	int error;

	/* A window whose controls are shown. */
	if (window == NULL)
		return EINVAL;
	if (window->titlebar == NULL)
		return ENOTSUP;

	/* The field takes the keyboard. */
	error = kl_titlebar_focus_control(window->titlebar, id, KL_FOCUS_FIELD);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Sets the state of an action, and sends it to the menu's items and the
 * controls of that action.
 */
int
kl_window_set_action_state(
	struct kl_window *window,
	uint32_t action,
	unsigned state)
{
	int error;
	int sent;

	/* A window, and only the states known. */
	if (window == NULL || (state & ~(KL_ACTION_DISABLED | KL_ACTION_CHECKED | KL_ACTION_HIDDEN)) != 0U)
		return EINVAL;

	/* The state kept, for what is shown and what is shown later. */
	error = keiui_declare_set_state(&window->action_states, action, state);
	if (error != 0)
		return error;

	/* The menu's items and the controls of the action (the popup takes it when it next opens). */
	error = 0;
	if (window->menu != NULL)
		error = keiui_declare_refresh(&window->menu_model, &window->action_states, declare_menu_sink, window->menu);
	if (window->titlebar != NULL) {
		sent = keiui_declare_refresh(&window->control_model, &window->action_states, declare_control_sink, window->titlebar);
		if (error == 0)
			error = sent;
	}

	/* The first refusal, if any. */
	return error;
}

/*
 * Opens a context menu of top-level items at a point of the window, for
 * its last press.
 */
int
kl_window_popup_menu(
	struct kl_window *window,
	const struct kl_menu_entry *entries,
	size_t count,
	int x,
	int y)
{
	struct kl_menu_service *service;
	int error;

	/* A window and some items. */
	if (window == NULL || entries == NULL || count == 0U)
		return EINVAL;

	/* The menu service; a compositor without the System Menu shows none. */
	service = declare_service(window);
	if (service == NULL)
		return ENOTSUP;

	/* The popup's menu, made once and changed to the table. */
	if (window->popup_menu == NULL) {
		window->popup_menu = kl_menu_create(service);
		if (window->popup_menu == NULL)
			return errno;
		keiui_declare_fini(&window->popup_model);
	}

	/* Its items as the table says. */
	error = keiui_declare_menu(&window->popup_model, entries, count, &window->action_states, declare_menu_sink, window->popup_menu);
	if (error != 0)
		return error;

	/* A context menu still open is replaced. */
	if (window->popup != NULL) {
		kl_context_menu_destroy(window->popup);
		window->popup = NULL;
	}

	/* zdesktop shows it at the press. */
	window->popup = kl_menu_popup(service, window->popup_menu, window->surface, x, y, window->seat, window->press_serial, &declare_popup_listener, window);
	if (window->popup == NULL)
		return errno;

	/* Succeeded. */
	return 0;
}

/*
 * Sets the window's glass panels for its next frame (count 0 takes them
 * away); the same panels again send nothing.
 */
int
kl_window_set_glass(
	struct kl_window *window,
	const struct kl_glass_panel *panels,
	size_t count)
{
	int same;
	int error;

	/* A window and a table, no longer than the compositor keeps. */
	if (window == NULL || (panels == NULL && count != 0U))
		return EINVAL;
	if (count > KL_GLASS_PANELS_MAX)
		return E2BIG;

	/* No glass and none asked for. */
	if (window->glass == NULL && count == 0U)
		return 0;

	/* The surface's glass, made once; a compositor without it shows none. */
	if (window->glass == NULL) {
		window->glass = kl_glass_create(window->display, window->surface);
		if (window->glass == NULL)
			return errno;
		window->glass_sent = 0;
	}

	/* The panels the compositor has already. */
	if (window->glass_sent && count == window->glass_count) {
		same = 1;
		if (count != 0U)
			same = memcmp(panels, window->glass_panels, sizeof(panels[0]) * count) == 0;
		if (same)
			return 0;
	}

	/* The new panels, kept to send only what changes. */
	error = kl_glass_set_panels(window->glass, panels, count);
	if (error != 0)
		return error;
	if (count != 0U)
		memcpy(window->glass_panels, panels, sizeof(panels[0]) * count);
	window->glass_count = count;
	window->glass_sent = 1;

	/* Succeeded: they go with the next frame. */
	return 0;
}

/*
 * Makes a Vulkan surface over the window, for the application's instance.
 */
int
kl_window_vulkan_surface(
	struct kl_window *window,
	VkInstance instance,
	VkSurfaceKHR *surface)
{
	VkWaylandSurfaceCreateInfoKHR info;
	VkResult result;

	/* A window, an instance and somewhere to put the surface. */
	if (window == NULL || instance == VK_NULL_HANDLE || surface == NULL)
		return EINVAL;

	/* The surface over the window's Wayland surface. */
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	info.display = window->display;
	info.surface = window->surface;
	result = vkCreateWaylandSurfaceKHR(instance, &info, NULL, surface);
	if (result != VK_SUCCESS)
		return EIO;

	/* Succeeded. */
	return 0;
}

/* The menu service the window's menus use: its application's, or one of its own made once. */
static struct kl_menu_service *
declare_service(
	struct kl_window *window)
{
	/* The application's. */
	if (window->app != NULL)
		return keiui_app_menu_service(window->app);

	/* The window's own. */
	if (window->menu_service == NULL)
		window->menu_service = kl_menu_service_open(window->display);
	return window->menu_service;
}

/* Carries out one operation of a menu's change on libkeiland's menu. */
static int
declare_menu_sink(
	void *data,
	unsigned operation,
	const struct keiui_declare_item *item)
{
	struct kl_menu *menu;
	int error;

	/* The menu, and what to do. */
	menu = data;
	switch (operation) {
	case KEIUI_DECLARE_BEGIN:
		return kl_menu_begin(menu);
	case KEIUI_DECLARE_COMMIT:
		return kl_menu_commit(menu);
	case KEIUI_DECLARE_REMOVE:
		/* One the compositor's copy no longer has (after a refusal) is gone already. */
		error = kl_menu_remove(menu, item->id);
		if (error == ENOENT)
			error = 0;
		return error;
	case KEIUI_DECLARE_APPEND:
		return kl_menu_append(menu, item->id, item->parent, item->type, item->label, item->action);
	case KEIUI_DECLARE_LABEL:
		return kl_menu_set_label(menu, item->id, item->label);
	case KEIUI_DECLARE_ACTION:
		return kl_menu_set_action(menu, item->id, item->action);
	case KEIUI_DECLARE_ROLE:
		return kl_menu_set_role(menu, item->id, item->role);
	case KEIUI_DECLARE_SHORTCUT:
		return kl_menu_set_shortcut(menu, item->id, item->modifiers, item->keysym);
	case KEIUI_DECLARE_STATE:
		break;
	default:
		return 0;
	}

	/* The state: enabled, shown, and checked for the items that can be. */
	error = kl_menu_set_enabled(menu, item->id, (item->state & KL_ACTION_DISABLED) == 0U);
	if (error == 0)
		error = kl_menu_set_visible(menu, item->id, (item->state & KL_ACTION_HIDDEN) == 0U);
	if (error == 0 && (item->type == KL_MENU_ITEM_CHECKBOX || item->type == KL_MENU_ITEM_RADIO))
		error = kl_menu_set_checked(menu, item->id, (item->state & KL_ACTION_CHECKED) != 0U);
	return error;
}

/* Carries out one operation of the controls' change on the titlebar. */
static int
declare_control_sink(
	void *data,
	unsigned operation,
	const struct keiui_declare_item *item)
{
	struct kl_titlebar *titlebar;
	int error;

	/* The titlebar, and what to do. */
	titlebar = data;
	switch (operation) {
	case KEIUI_DECLARE_BEGIN:
		return kl_titlebar_begin(titlebar);
	case KEIUI_DECLARE_COMMIT:
		return kl_titlebar_commit(titlebar);
	case KEIUI_DECLARE_REMOVE:
		/* One the compositor's copy no longer has (after a refusal) is gone already. */
		error = kl_titlebar_remove_control(titlebar, item->id);
		if (error == ENOENT)
			error = 0;
		return error;
	case KEIUI_DECLARE_APPEND:
		return kl_titlebar_add_control(titlebar, item->id, item->role, item->type, item->parent, item->label);
	case KEIUI_DECLARE_LABEL:
		return kl_titlebar_set_control_label(titlebar, item->id, item->label);
	case KEIUI_DECLARE_STATE:
		return kl_titlebar_set_control_state(titlebar, item->id, (item->state & KL_ACTION_DISABLED) == 0U, (item->state & KL_ACTION_CHECKED) != 0U);
	default:
		break;
	}

	/* An action is the library's own; nothing else is sent for a control. */
	return 0;
}

/* Gives the window its titlebar in the controls mode, once; 0 or an errno value. */
static int
declare_titlebar(
	struct kl_window *window)
{
	int error;

	/* Made already. */
	if (window->titlebar != NULL)
		return 0;

	/* The titlebar; a compositor without it shows no controls. */
	window->titlebar = kl_titlebar_create(window->display, window->toplevel, &declare_titlebar_listener, window);
	if (window->titlebar == NULL)
		return errno;

	/* The controls mode, in a transaction of its own. */
	error = kl_titlebar_begin(window->titlebar);
	if (error == 0)
		error = kl_titlebar_set_mode(window->titlebar, KL_TITLEBAR_CONTROLS);
	if (error == 0)
		error = kl_titlebar_commit(window->titlebar);

	/* Refused: no titlebar, and nothing kept. */
	if (error != 0) {
		kl_titlebar_destroy(window->titlebar);
		window->titlebar = NULL;
		return error;
	}

	/* Succeeded: nothing shown in it yet. */
	keiui_declare_fini(&window->control_model);
	return 0;
}

/* Queues an action chosen among the window's inputs. */
static void
declare_action(
	struct kl_window *window,
	uint32_t action,
	uint32_t id,
	uint32_t detail)
{
	struct kl_window_event *event;

	/* The input; a full queue drops it. */
	event = keiui_window_push(window, KL_WINDOW_ACTION);
	if (event == NULL)
		return;
	event->code = action;
	event->id = (int32_t)id;
	event->begin = (int32_t)detail;
}

/* An item of the window's menu was chosen. */
static void
declare_menu_activated(
	void *data,
	struct kl_window_menu *window_menu,
	uint32_t item,
	uint32_t action,
	struct wl_seat *seat,
	uint32_t serial)
{
	struct kl_window *window;

	/* The choice is the window's last input (for the clipboard), and its action goes among the inputs. */
	(void)window_menu;
	(void)seat;
	window = data;
	window->serial = serial;
	declare_action(window, action, item, 0U);
}

/* An item of the context menu was chosen. */
static void
declare_popup_activated(
	void *data,
	struct kl_context_menu *context_menu,
	uint32_t item,
	uint32_t action,
	uint32_t serial)
{
	struct kl_window *window;

	/* The choice is the window's last input (for the clipboard), and its action goes among the inputs. */
	(void)context_menu;
	window = data;
	window->serial = serial;
	declare_action(window, action, item, 0U);
}

/* The context menu closed (told last, once): it is destroyed. */
static void
declare_popup_done(
	void *data,
	struct kl_context_menu *context_menu)
{
	struct kl_window *window;

	/* The one open is the one that closed. */
	window = data;
	if (window->popup == context_menu) {
		kl_context_menu_destroy(window->popup);
		window->popup = NULL;
	}
}

/* A control of the titlebar was chosen: its action, among the inputs. */
static void
declare_control_activated(
	void *data,
	struct kl_titlebar *titlebar,
	uint32_t id,
	uint32_t detail,
	struct wl_seat *seat,
	uint32_t serial)
{
	const struct keiui_declare_item *control;
	struct kl_window *window;

	/* The control of the ID, when the window shows it; the choice is the window's last input. */
	(void)titlebar;
	(void)seat;
	window = data;
	window->serial = serial;
	control = keiui_declare_find(&window->control_model, id);
	if (control == NULL)
		return;

	/* Its action. */
	declare_action(window, control->action, id, detail);
}

/* A field's text as it is typed: among the inputs (KL_VERSION 43). */
static void
declare_text_changed(
	void *data,
	struct kl_titlebar *titlebar,
	uint32_t id,
	const char *text)
{
	/* The input. */
	(void)titlebar;
	declare_text(data, KL_WINDOW_CONTROL_TEXT, id, text, 0U);
}

/* A field's editing ended: its text and how, among the inputs (KL_VERSION 43). */
static void
declare_text_done(
	void *data,
	struct kl_titlebar *titlebar,
	uint32_t id,
	const char *text,
	unsigned how)
{
	/* The input. */
	(void)titlebar;
	declare_text(data, KL_WINDOW_CONTROL_DONE, id, text, how);
}

/* Queues a field's text among the window's inputs (a full queue drops it; a long text is cut). */
static void
declare_text(
	struct kl_window *window,
	unsigned kind,
	uint32_t id,
	const char *text,
	unsigned how)
{
	struct kl_window_event *event;
	size_t length;

	/* The input. */
	event = keiui_window_push(window, kind);
	if (event == NULL)
		return;
	event->id = (int32_t)id;
	event->code = how;

	/* Its text, cut at a character's start when it is too long. */
	length = 0U;
	if (text != NULL)
		length = strlen(text);
	if (length >= sizeof(event->text)) {
		length = sizeof(event->text) - 1U;
		while (length > 0U && ((unsigned char)text[length] & 0xc0U) == 0x80U)
			length--;
	}
	if (length != 0U)
		memcpy(event->text, text, length);
	event->text[length] = '\0';
}
