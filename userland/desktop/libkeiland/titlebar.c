/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Titlebar Presentation (WS070 p008, plan/ws070/titlebar-design.md
 * section 4): the wrapper of zdesktop's keiland_titlebar_v1 protocol.
 *
 * A titlebar keeps a mirror of its controls' IDs and roles and of its tabs'
 * IDs as the requests sent so far leave them, so that a call the
 * compositor would refuse -- with a protocol error that ends the whole
 * connection -- is refused here as one failed call instead.  The titlebar
 * lives on its window's queue, so its events arrive where the application
 * dispatches its window's.
 */

#include <keiland.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include "userland/desktop/libwayland/zed-titlebar-v1-client-protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The oldest version of the protocol this library speaks, and the newest (2: drop_target). */
#define TITLEBAR_VERSION	1U
#define TITLEBAR_VERSION_DROP	2U

/* The compositor's bounds of one titlebar (plan/ws070/titlebar-design.md section 2.2). */
#define TITLEBAR_CONTROLS_MAX	64U
#define TITLEBAR_TABS_MAX	128U
#define TITLEBAR_SEGMENTS_MAX	32U
#define TITLEBAR_TEXT_MAX	1023U
#define TITLEBAR_ROLE_LAST	16U
#define TITLEBAR_PRIORITY_LAST	2U
#define TITLEBAR_MODE_LAST	2U
#define TITLEBAR_FLAGS_ALL	7U
#define TITLEBAR_OPTIONS_ALL	1U

/*
 * One control as the mirror knows it: its ID, its role, and whether a
 * commit has shown it (only a shown control can take the keyboard).
 */
struct titlebar_entry {
	uint32_t id;
	unsigned role;
	unsigned committed;
};

/*
 * One window's titlebar: its keiland_titlebar_v1, the application's listener,
 * the mirror of its controls and tabs, whether a transaction is open, and
 * the serial of the last one.
 */
struct keiland_titlebar {
	struct keiland_titlebar_v1 *proxy;
	const struct keiland_titlebar_listener *listener;
	void *data;
	struct titlebar_entry controls[TITLEBAR_CONTROLS_MAX];
	unsigned control_count;
	uint32_t tabs[TITLEBAR_TABS_MAX];
	unsigned tab_count;
	unsigned updating;
	uint32_t serial;
};

/* What the registry search found: the manager's global name (0 for none) and its version. */
struct titlebar_search {
	uint32_t name;
	uint32_t version;
};

static struct keiland_titlebar_manager_v1 *titlebar_bind(struct wl_display *display);
static void titlebar_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void titlebar_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void titlebar_activated(void *data, struct keiland_titlebar_v1 *proxy, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void titlebar_text_changed(void *data, struct keiland_titlebar_v1 *proxy, uint32_t id, const char *text);
static void titlebar_text_done(void *data, struct keiland_titlebar_v1 *proxy, uint32_t id, const char *text, uint32_t how);
static void titlebar_tab_activated(void *data, struct keiland_titlebar_v1 *proxy, uint32_t id, uint32_t serial);
static void titlebar_tab_close(void *data, struct keiland_titlebar_v1 *proxy, uint32_t id);
static void titlebar_new_tab(void *data, struct keiland_titlebar_v1 *proxy, uint32_t serial);
static void titlebar_overflow(void *data, struct keiland_titlebar_v1 *proxy);
static void titlebar_drop_target(void *data, struct keiland_titlebar_v1 *proxy, uint32_t id, uint32_t detail);
static struct titlebar_entry *titlebar_control(struct keiland_titlebar *titlebar, uint32_t id);
static int titlebar_tab(const struct keiland_titlebar *titlebar, uint32_t id);
static int titlebar_text_ok(const char *text);

/* The registry's callbacks while the manager is looked for. */
static const struct wl_registry_listener titlebar_registry_listener = {
	titlebar_global, titlebar_global_remove
};

/* The titlebar's events, handed on to the application's listener. */
static const struct keiland_titlebar_v1_listener titlebar_listener = {
	titlebar_activated,
	titlebar_text_changed,
	titlebar_text_done,
	titlebar_tab_activated,
	titlebar_tab_close,
	titlebar_new_tab,
	titlebar_overflow,
	titlebar_drop_target
};

/*
 * Gives a window its titlebar presentation, in menu mode until changed.
 * Returns NULL with errno set: ENOTSUP for a compositor without the
 * protocol, ENOMEM or EINVAL when the objects cannot be made.
 */
struct keiland_titlebar *
keiland_titlebar_create(
	struct wl_display *display,
	struct xdg_toplevel *toplevel,
	const struct keiland_titlebar_listener *listener,
	void *data)
{
	struct keiland_titlebar_manager_v1 *manager;
	struct keiland_titlebar *titlebar;
	struct wl_event_queue *queue;
	int status;

	/* zdesktop's manager, bound for this window. */
	manager = titlebar_bind(display);
	if (manager == NULL)
		return NULL;

	/* The record with the application's listener and an empty mirror. */
	titlebar = calloc(1, sizeof(*titlebar));
	if (titlebar == NULL) {
		keiland_titlebar_manager_v1_destroy(manager);
		errno = ENOMEM;
		return NULL;
	}

	/* The application's callbacks and their argument. */
	titlebar->listener = listener;
	titlebar->data = data;

	/* The protocol object; the binding is not needed after it (the titlebar stays). */
	titlebar->proxy = keiland_titlebar_manager_v1_get_titlebar(manager, toplevel);
	keiland_titlebar_manager_v1_destroy(manager);
	if (titlebar->proxy == NULL) {
		free(titlebar);
		errno = ENOMEM;
		return NULL;
	}

	/* Its events arrive with the window's. */
	queue = wl_proxy_get_queue((struct wl_proxy *)toplevel);
	wl_proxy_set_queue((struct wl_proxy *)titlebar->proxy, queue);

	/* The library's listener hands them on. */
	status = keiland_titlebar_v1_add_listener(titlebar->proxy, &titlebar_listener, titlebar);
	if (status != 0) {
		keiland_titlebar_v1_destroy(titlebar->proxy);
		free(titlebar);
		errno = EINVAL;
		return NULL;
	}

	/* Succeeded: the window's titlebar can be given a presentation. */
	return titlebar;
}

/*
 * Takes the titlebar presentation away; the window shows its menu again.
 */
void
keiland_titlebar_destroy(
	struct keiland_titlebar *titlebar)
{
	/* No titlebar, nothing to destroy. */
	if (titlebar == NULL)
		return;

	/* The protocol object, then the record. */
	keiland_titlebar_v1_destroy(titlebar->proxy);
	free(titlebar);
}

/*
 * Starts a transaction.
 */
int
keiland_titlebar_begin(
	struct keiland_titlebar *titlebar)
{
	/* One at a time. */
	if (titlebar->updating != 0U)
		return EBUSY;

	/* A new serial names the transaction. */
	titlebar->serial++;
	titlebar->updating = 1;
	keiland_titlebar_v1_begin_update(titlebar->proxy, titlebar->serial);

	/* Succeeded: changes may follow. */
	return 0;
}

/*
 * Ends a transaction; zdesktop shows its changes at once.
 */
int
keiland_titlebar_commit(
	struct keiland_titlebar *titlebar)
{
	unsigned index;

	/* Only an open transaction ends. */
	if (titlebar->updating == 0U)
		return EINVAL;

	/* The controls are shown from now on (they may take the keyboard). */
	for (index = 0; index < titlebar->control_count; index++)
		titlebar->controls[index].committed = 1;

	/* The commit names the transaction's serial. */
	titlebar->updating = 0;
	keiland_titlebar_v1_commit(titlebar->proxy, titlebar->serial);

	/* Succeeded: the changes are shown together. */
	return 0;
}

/*
 * Chooses the presentation.
 */
int
keiland_titlebar_set_mode(
	struct keiland_titlebar *titlebar,
	unsigned mode)
{
	/* Inside a transaction, one of the three modes. */
	if (titlebar->updating == 0U || mode > TITLEBAR_MODE_LAST)
		return EINVAL;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_mode(titlebar->proxy, mode);
	return 0;
}

/*
 * Adds a control at the end.
 */
int
keiland_titlebar_add_control(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	unsigned role,
	unsigned priority,
	unsigned group,
	const char *label)
{
	struct titlebar_entry *entry;
	int fits;

	/* Inside a transaction, a known role and priority, and a label. */
	if (titlebar->updating == 0U || id == 0U || label == NULL)
		return EINVAL;
	if (role == 0U || role > TITLEBAR_ROLE_LAST || priority > TITLEBAR_PRIORITY_LAST)
		return EINVAL;

	/* A new ID. */
	entry = titlebar_control(titlebar, id);
	if (entry != NULL)
		return EEXIST;

	/* Room for it, and a label not too long. */
	fits = titlebar_text_ok(label);
	if (titlebar->control_count == TITLEBAR_CONTROLS_MAX || fits == 0)
		return E2BIG;

	/* The mirror notes it, not yet shown. */
	entry = &titlebar->controls[titlebar->control_count];
	entry->id = id;
	entry->role = role;
	entry->committed = 0;
	titlebar->control_count++;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_add_control(titlebar->proxy, id, role, priority, group, label);
	return 0;
}

/*
 * Removes a control.
 */
int
keiland_titlebar_remove_control(
	struct keiland_titlebar *titlebar,
	uint32_t id)
{
	struct titlebar_entry *entry;
	size_t index;

	/* Inside a transaction, a control that is there. */
	if (titlebar->updating == 0U)
		return EINVAL;
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;

	/* The mirror forgets it, the ones after it closing up. */
	index = (size_t)(entry - titlebar->controls);
	memmove(entry, entry + 1, (titlebar->control_count - index - 1U) * sizeof(*entry));
	titlebar->control_count--;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_remove_control(titlebar->proxy, id);
	return 0;
}

/*
 * Sets a control's label.
 */
int
keiland_titlebar_set_control_label(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	const char *label)
{
	struct titlebar_entry *entry;
	int fits;

	/* Inside a transaction, a label, a control that is there. */
	if (titlebar->updating == 0U || label == NULL)
		return EINVAL;
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;

	/* A label not too long. */
	fits = titlebar_text_ok(label);
	if (fits == 0)
		return E2BIG;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_control_label(titlebar->proxy, id, label);
	return 0;
}

/*
 * Sets whether a control works now and whether it is checked (any nonzero is yes).
 */
int
keiland_titlebar_set_control_state(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	int enabled,
	int checked)
{
	struct titlebar_entry *entry;
	uint32_t enabled_word;
	uint32_t checked_word;

	/* Inside a transaction, a control that is there. */
	if (titlebar->updating == 0U)
		return EINVAL;
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;

	/* The protocol takes 0 or 1. */
	enabled_word = 0;
	if (enabled != 0)
		enabled_word = 1;
	checked_word = 0;
	if (checked != 0)
		checked_word = 1;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_control_state(titlebar->proxy, id, enabled_word, checked_word);
	return 0;
}

/*
 * Sets a progress control's share done.
 */
int
keiland_titlebar_set_control_value(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	unsigned value)
{
	struct titlebar_entry *entry;

	/* Inside a transaction, a progress control that is there, a value it takes. */
	if (titlebar->updating == 0U || value > KEILAND_PROGRESS_UNKNOWN)
		return EINVAL;
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;
	if (entry->role != KEILAND_CONTROL_PROGRESS)
		return EINVAL;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_control_value(titlebar->proxy, id, value);
	return 0;
}

/*
 * Sets a search's or a breadcrumb's text and placeholder (NULL is empty).
 */
int
keiland_titlebar_set_control_text(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	const char *text,
	const char *placeholder)
{
	struct titlebar_entry *entry;
	int text_fits;
	int placeholder_fits;

	/* Inside a transaction, a search or a breadcrumb that is there. */
	if (titlebar->updating == 0U)
		return EINVAL;
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;
	if (entry->role != KEILAND_CONTROL_SEARCH && entry->role != KEILAND_CONTROL_BREADCRUMB)
		return EINVAL;

	/* No text is an empty one. */
	if (text == NULL)
		text = "";
	if (placeholder == NULL)
		placeholder = "";

	/* Texts not too long. */
	text_fits = titlebar_text_ok(text);
	placeholder_fits = titlebar_text_ok(placeholder);
	if (text_fits == 0 || placeholder_fits == 0)
		return E2BIG;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_control_text(titlebar->proxy, id, text, placeholder);
	return 0;
}

/*
 * Sets a breadcrumb's parts.
 */
int
keiland_titlebar_set_breadcrumb(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	const char *const *segments,
	size_t count)
{
	struct titlebar_entry *entry;
	struct wl_array parts;
	char *place;
	size_t length;
	size_t index;
	int fits;

	/* Inside a transaction, a breadcrumb that is there, not too many parts. */
	if (titlebar->updating == 0U)
		return EINVAL;
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;
	if (entry->role != KEILAND_CONTROL_BREADCRUMB)
		return EINVAL;
	if (count > TITLEBAR_SEGMENTS_MAX)
		return E2BIG;

	/* Each part, there and not too long. */
	for (index = 0; index < count; index++) {
		if (segments[index] == NULL)
			return EINVAL;
		fits = titlebar_text_ok(segments[index]);
		if (fits == 0)
			return E2BIG;
	}

	/* The parts one after another, each ended by its NUL. */
	wl_array_init(&parts);
	for (index = 0; index < count; index++) {
		length = strlen(segments[index]) + 1U;
		place = wl_array_add(&parts, length);
		if (place == NULL) {
			wl_array_release(&parts);
			return ENOMEM;
		}

		/* The part and its NUL. */
		memcpy(place, segments[index], length);
	}

	/* The request, then the array goes. */
	keiland_titlebar_v1_set_breadcrumb(titlebar->proxy, id, &parts);
	wl_array_release(&parts);

	/* Succeeded: the request is sent. */
	return 0;
}

/*
 * Adds a tab at the end.
 */
int
keiland_titlebar_add_tab(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	const char *title)
{
	int there;
	int fits;

	/* Inside a transaction, an ID that is not 0, a title. */
	if (titlebar->updating == 0U || id == 0U || title == NULL)
		return EINVAL;

	/* A new ID. */
	there = titlebar_tab(titlebar, id);
	if (there >= 0)
		return EEXIST;

	/* Room for it, and a title not too long. */
	fits = titlebar_text_ok(title);
	if (titlebar->tab_count == TITLEBAR_TABS_MAX || fits == 0)
		return E2BIG;

	/* The mirror notes it. */
	titlebar->tabs[titlebar->tab_count] = id;
	titlebar->tab_count++;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_add_tab(titlebar->proxy, id, title);
	return 0;
}

/*
 * Removes a tab.
 */
int
keiland_titlebar_remove_tab(
	struct keiland_titlebar *titlebar,
	uint32_t id)
{
	int index;

	/* Inside a transaction, a tab that is there. */
	if (titlebar->updating == 0U)
		return EINVAL;
	index = titlebar_tab(titlebar, id);
	if (index < 0)
		return ENOENT;

	/* The mirror forgets it, the ones after it closing up. */
	memmove(&titlebar->tabs[index], &titlebar->tabs[index + 1], (titlebar->tab_count - (unsigned)index - 1U) * sizeof(titlebar->tabs[0]));
	titlebar->tab_count--;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_remove_tab(titlebar->proxy, id);
	return 0;
}

/*
 * Sets a tab's title and flags.
 */
int
keiland_titlebar_set_tab(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	const char *title,
	unsigned flags)
{
	int index;
	int fits;

	/* Inside a transaction, a title, the flags known. */
	if (titlebar->updating == 0U || title == NULL)
		return EINVAL;
	if ((flags & ~TITLEBAR_FLAGS_ALL) != 0U)
		return EINVAL;

	/* A tab that is there. */
	index = titlebar_tab(titlebar, id);
	if (index < 0)
		return ENOENT;

	/* A title not too long. */
	fits = titlebar_text_ok(title);
	if (fits == 0)
		return E2BIG;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_tab(titlebar->proxy, id, title, flags);
	return 0;
}

/*
 * Sets the tab strip's options.
 */
int
keiland_titlebar_set_tabs_options(
	struct keiland_titlebar *titlebar,
	unsigned options)
{
	/* Inside a transaction, the options known. */
	if (titlebar->updating == 0U)
		return EINVAL;
	if ((options & ~TITLEBAR_OPTIONS_ALL) != 0U)
		return EINVAL;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_set_tabs_options(titlebar->proxy, options);
	return 0;
}

/*
 * Gives the keyboard to a committed search or breadcrumb control.
 */
int
keiland_titlebar_focus_control(
	struct keiland_titlebar *titlebar,
	uint32_t id,
	unsigned mode)
{
	struct titlebar_entry *entry;

	/* A control that is there and was shown. */
	entry = titlebar_control(titlebar, id);
	if (entry == NULL)
		return ENOENT;
	if (entry->committed == 0U)
		return EINVAL;

	/* A search takes it as a field, a breadcrumb as a field or as a path to edit. */
	if (entry->role != KEILAND_CONTROL_SEARCH && entry->role != KEILAND_CONTROL_BREADCRUMB)
		return EINVAL;
	if (mode > KEILAND_FOCUS_EDIT)
		return EINVAL;

	/* Succeeded: the request is sent. */
	keiland_titlebar_v1_focus_control(titlebar->proxy, id, mode);
	return 0;
}

/*
 * Finds and binds zdesktop's keiland_titlebar_manager_v1 on a queue of its own,
 * so that no event of the application's is dispatched by the search; the
 * binding is moved to the display's default queue.  Returns NULL with errno
 * set.
 */
static struct keiland_titlebar_manager_v1 *
titlebar_bind(
	struct wl_display *display)
{
	struct keiland_titlebar_manager_v1 *manager;
	struct titlebar_search search;
	struct wl_event_queue *queue;
	struct wl_display *wrapper;
	struct wl_registry *registry;
	uint32_t version;
	int status;

	/* The search's own queue, and the display as seen from it. */
	queue = wl_display_create_queue(display);
	if (queue == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* The display as the search sees it. */
	wrapper = wl_proxy_create_wrapper(display);
	if (wrapper == NULL) {
		wl_event_queue_destroy(queue);
		errno = ENOMEM;
		return NULL;
	}

	/* What the wrapper makes lives on the search's queue. */
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

	/* The globals, announced to this search alone. */
	search.name = 0;
	search.version = 0;
	registry = wl_display_get_registry(wrapper);
	if (registry != NULL) {
		status = wl_registry_add_listener(registry, &titlebar_registry_listener, &search);
		if (status == 0)
			(void)wl_display_roundtrip_queue(display, queue);
	}

	/* The newest version both sides speak. */
	version = TITLEBAR_VERSION;
	if (search.version >= TITLEBAR_VERSION_DROP)
		version = TITLEBAR_VERSION_DROP;

	/* The manager, bound when announced, is moved to the application's default queue. */
	manager = NULL;
	if (registry != NULL && search.name != 0U) {
		manager = wl_registry_bind(registry, search.name, &keiland_titlebar_manager_v1_interface, version);
		if (manager != NULL)
			wl_proxy_set_queue((struct wl_proxy *)manager, NULL);
	}

	/* The search's objects go. */
	if (registry != NULL)
		wl_registry_destroy(registry);
	wl_proxy_wrapper_destroy(wrapper);
	wl_event_queue_destroy(queue);

	/* A compositor without the Titlebar Presentation. */
	if (search.name == 0U) {
		errno = ENOTSUP;
		return NULL;
	}

	/* The binding could not be made. */
	if (manager == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* Succeeded: the manager. */
	return manager;
}

/* Notes the manager's global name when the registry announces it. */
static void
titlebar_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct titlebar_search *search;
	int same;

	/* Only the manager, at a version this library speaks. */
	(void)registry;
	search = data;
	same = strcmp(interface, "keiland_titlebar_manager_v1");
	if (same != 0 || version < TITLEBAR_VERSION)
		return;

	/* The first one found is used, at the version it has. */
	if (search->name == 0U) {
		search->name = name;
		search->version = version;
	}
}

/* A global going away during the search changes nothing. */
static void
titlebar_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to do. */
	(void)data;
	(void)registry;
	(void)name;
}

/* Hands a chosen control on to the application's listener. */
static void
titlebar_activated(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t id,
	uint32_t detail,
	struct wl_seat *seat,
	uint32_t serial)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->control_activated == NULL)
		return;

	/* The choice. */
	titlebar->listener->control_activated(titlebar->data, titlebar, id, detail, seat, serial);
}

/* Hands a text control's new text on to the application's listener. */
static void
titlebar_text_changed(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t id,
	const char *text)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->text_changed == NULL)
		return;

	/* The text. */
	titlebar->listener->text_changed(titlebar->data, titlebar, id, text);
}

/* Hands the end of a text control's editing on to the application's listener. */
static void
titlebar_text_done(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t id,
	const char *text,
	uint32_t how)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->text_done == NULL)
		return;

	/* The text and how the editing ended. */
	titlebar->listener->text_done(titlebar->data, titlebar, id, text, how);
}

/* Hands a chosen tab on to the application's listener. */
static void
titlebar_tab_activated(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t id,
	uint32_t serial)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->tab_activated == NULL)
		return;

	/* The tab. */
	titlebar->listener->tab_activated(titlebar->data, titlebar, id, serial);
}

/* Hands a tab's close button on to the application's listener. */
static void
titlebar_tab_close(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t id)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->tab_close_requested == NULL)
		return;

	/* The tab. */
	titlebar->listener->tab_close_requested(titlebar->data, titlebar, id);
}

/* Hands the new-tab button on to the application's listener. */
static void
titlebar_new_tab(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t serial)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->new_tab_requested == NULL)
		return;

	/* The request. */
	titlebar->listener->new_tab_requested(titlebar->data, titlebar, serial);
}

/* Hands the overflow popup's opening on to the application's listener. */
static void
titlebar_overflow(
	void *data,
	struct keiland_titlebar_v1 *proxy)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->overflow_menu_opened == NULL)
		return;

	/* The notice. */
	titlebar->listener->overflow_menu_opened(titlebar->data, titlebar);
}

/* Hands a drag and drop's place over a control's part (or over none) on to the application's listener. */
static void
titlebar_drop_target(
	void *data,
	struct keiland_titlebar_v1 *proxy,
	uint32_t id,
	uint32_t detail)
{
	struct keiland_titlebar *titlebar;

	/* The application's callback, if it has one. */
	(void)proxy;
	titlebar = data;
	if (titlebar->listener == NULL || titlebar->listener->drop_target == NULL)
		return;

	/* The place. */
	titlebar->listener->drop_target(titlebar->data, titlebar, id, detail);
}

/* Finds a control in the mirror; NULL when there is none. */
static struct titlebar_entry *
titlebar_control(
	struct keiland_titlebar *titlebar,
	uint32_t id)
{
	unsigned index;

	/* Each control, for the ID. */
	for (index = 0; index < titlebar->control_count; index++) {
		if (titlebar->controls[index].id == id)
			return &titlebar->controls[index];
	}

	/* No control has the ID. */
	return NULL;
}

/* Finds a tab in the mirror; its index, or -1 when there is none. */
static int
titlebar_tab(
	const struct keiland_titlebar *titlebar,
	uint32_t id)
{
	unsigned index;

	/* Each tab, for the ID. */
	for (index = 0; index < titlebar->tab_count; index++) {
		if (titlebar->tabs[index] == id)
			return (int)index;
	}

	/* No tab has the ID. */
	return -1;
}

/* Tells whether a text is not longer than the compositor takes. */
static int
titlebar_text_ok(
	const char *text)
{
	size_t length;

	/* The text's bytes. */
	length = strlen(text);
	if (length > TITLEBAR_TEXT_MAX)
		return 0;

	/* It fits. */
	return 1;
}
