/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The inside of the DOM binding: the window's record, the tables that
 * describe each interface, and the helpers the interfaces share.
 *
 * An interface is described by a table (its name, its parent, its
 * attributes with their getters and setters, its operations and its
 * constants) from which window.c makes the interface object and its
 * prototype.  The tables are what a WebIDL generator would write; they are
 * written by hand while the interfaces are few.
 */

#ifndef ZDESKTOP_BROWSER_BIND_INTERNAL_H
#define ZDESKTOP_BROWSER_BIND_INTERNAL_H

#include "bind/bind.h"

/*
 * The interfaces the binding makes, parents before children (window.c
 * makes them in this order, so a prototype's parent exists when it is
 * made).
 */
enum bind_interface_index {
	BIND_EVENT_TARGET,
	BIND_NODE,
	BIND_CHARACTER_DATA,
	BIND_TEXT,
	BIND_COMMENT,
	BIND_DOCUMENT,
	BIND_DOCUMENT_TYPE,
	BIND_DOCUMENT_FRAGMENT,
	BIND_ELEMENT,
	BIND_HTML_ELEMENT,
	BIND_WINDOW,
	BIND_EVENT,
	BIND_UI_EVENT,
	BIND_MOUSE_EVENT,
	BIND_CUSTOM_EVENT,
	BIND_INTERFACES
};

/* The parent of an interface that has none. */
#define BIND_NO_PARENT		(-1)

/*
 * One attribute of an interface: an accessor on the prototype with a
 * native getter and, unless the attribute is read-only, a native setter.
 */
struct bind_attribute {
	const char *name;
	vm_native getter;
	vm_native setter;
};

/*
 * One operation of an interface: a method on the prototype.
 */
struct bind_operation {
	const char *name;
	unsigned length;
	vm_native method;
};

/*
 * One constant of an interface, on both the interface object and its
 * prototype.
 */
struct bind_constant {
	const char *name;
	int value;
};

/*
 * An interface: what window.c needs to make its interface object (a
 * constructor, which throws unless the interface has one) and its
 * prototype.  Each list ends with an entry whose name is NULL; a NULL list
 * is empty.
 */
struct bind_interface {
	const char *name;
	int parent;
	unsigned length;
	vm_native construct;
	const struct bind_attribute *attributes;
	const struct bind_operation *operations;
	const struct bind_constant *constants;
};

/*
 * A timer of setTimeout, setInterval or requestAnimationFrame: when it is
 * due (in the window's milliseconds), its period for an interval, what it
 * calls and with what, and the order it was made in (timers due at the
 * same time run in that order).
 */
struct bind_timer {
	uint32_t id;
	int repeat;
	int animation;
	double due;
	double interval;
	uint64_t sequence;
	vm_value callback;
	vm_value arguments;
};

/*
 * The window of a page: the realm whose global object it is, the
 * document, the host, the prototypes of the interfaces, the window's own
 * event listeners, the timers, and the time and viewport the page gave
 * it.
 *
 * It lives from bind_window_create to bind_window_destroy; its tracer
 * keeps the cells it holds alive.  ready_state is a static string.
 */
struct bind_window {
	struct vm_realm *realm;
	struct dom_document *document;
	struct bind_host host;
	struct vm_object *prototypes[BIND_INTERFACES];
	struct vm_object *console;
	struct vm_cell *listeners;
	struct wb_vector timers;
	uint32_t next_timer_id;
	uint64_t next_sequence;
	double now;
	int viewport_width;
	int viewport_height;
	const char *ready_state;
};

/*
 * The event listeners of one event target: a cell the target keeps (a
 * node's listeners field, or the window's), with the listeners in the
 * order they were added.
 */
struct bind_listener {
	struct vm_string *type;
	vm_value callback;
	int capture;
	int once;
};

/*
 * The cell of an event target's listeners.
 */
struct bind_listeners {
	struct vm_cell cell;
	struct bind_listener *items;
	size_t count;
	size_t capacity;
};

/*
 * The state of an event, the cell an Event object wraps: its type, where
 * it is being dispatched, its flags and, for a mouse event, the pointer.
 * target_override is the target the event reports instead of the one it
 * is dispatched at (the document for the window's load), or the empty
 * value.
 */
struct bind_event {
	struct vm_cell cell;
	struct vm_string *type;
	vm_value target;
	vm_value current_target;
	vm_value detail;
	vm_value target_override;
	int phase;
	int bubbles;
	int cancelable;
	int canceled;
	int stop;
	int stop_immediate;
	int dispatching;
	int trusted;
	double time_stamp;
	struct bind_mouse mouse;
};

/* The phases of an event (Event.eventPhase). */
#define BIND_PHASE_NONE		0
#define BIND_PHASE_CAPTURING	1
#define BIND_PHASE_AT_TARGET	2
#define BIND_PHASE_BUBBLING	3

/* The interface tables (node.c, element.c, document.c, event.c, window.c). */
extern const struct bind_interface bind_event_target_interface;
extern const struct bind_interface bind_node_interface;
extern const struct bind_interface bind_character_data_interface;
extern const struct bind_interface bind_text_interface;
extern const struct bind_interface bind_comment_interface;
extern const struct bind_interface bind_document_interface;
extern const struct bind_interface bind_document_type_interface;
extern const struct bind_interface bind_document_fragment_interface;
extern const struct bind_interface bind_element_interface;
extern const struct bind_interface bind_html_element_interface;
extern const struct bind_interface bind_window_interface;
extern const struct bind_interface bind_event_interface;
extern const struct bind_interface bind_ui_event_interface;
extern const struct bind_interface bind_mouse_event_interface;
extern const struct bind_interface bind_custom_event_interface;

/* The window and the shared helpers (window.c). */
struct bind_window *bind_window_of(struct vm_realm *realm);
int bind_window_install(struct bind_window *window);
int bind_throw_illegal(struct vm_realm *realm);
int bind_illegal_constructor(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_string(struct vm_realm *realm, const char *ascii, vm_value *value);
int bind_units(struct vm_realm *realm, const uint16_t *units, size_t length, vm_value *value);
int bind_to_string(struct vm_realm *realm, vm_value value, struct vm_string **string);
int bind_to_atom(struct vm_realm *realm, vm_value value, int lower, struct vm_string **atom);
int bind_get_option(struct vm_realm *realm, vm_value options, const char *name, int *present, vm_value *value);

/* Nodes (node.c). */
int bind_wrap(struct bind_window *window, struct dom_node *node, vm_value *value);
int bind_wrap_or_null(struct bind_window *window, struct dom_node *node, vm_value *value);
struct dom_node *bind_node_of(vm_value value);
int bind_this_node(struct vm_realm *realm, vm_value this_value, struct dom_node **node);
int bind_argument_node(struct vm_realm *realm, vm_value value, struct dom_node **node);
int bind_text_content(const struct dom_node *node, struct wb_units *units);
int bind_insert(struct vm_realm *realm, struct dom_node *parent, struct dom_node *node, struct dom_node *reference);
int bind_array_create(struct vm_realm *realm, struct vm_object **array);
int bind_array_push_node(struct bind_window *window, struct vm_object *array, struct dom_node *node);
int bind_throw_dom(struct vm_realm *realm, const char *name, const char *message);
struct dom_node *bind_following(const struct dom_node *node, const struct dom_node *root);
struct dom_element *bind_first_element_child(const struct dom_node *node);

/* The mixins' getters and methods shared by several interfaces (node.c). */
int bind_children(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_first_element_child_get(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_last_element_child_get(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_child_element_count(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_append(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_prepend(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_remove(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_previous_element_sibling(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_next_element_sibling(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_get_elements_by_tag_name(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_get_elements_by_class_name(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);

/* Elements (element.c). */
int bind_element_has_class(const struct dom_element *element, const struct vm_string *name);

/* Events (event.c). */
struct bind_event *bind_event_of(vm_value value);
int bind_event_create(struct bind_window *window, int interface, struct vm_string *type, vm_value *value, struct bind_event **event);
int bind_dispatch(struct bind_window *window, vm_value target, vm_value event_value, int *canceled);
int bind_listeners_of(struct bind_window *window, vm_value target, int create, struct bind_listeners **listeners);
void bind_listeners_trace(struct vm_heap *heap, struct bind_listeners *listeners);

/* The timers (timer.c). */
void bind_timers_trace(struct vm_heap *heap, struct bind_window *window);
int bind_set_timeout(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_set_interval(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_clear_timer(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
int bind_request_animation_frame(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);

#endif
