/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DOM binding (plan/ws074/design.md §12.4): what a page's scripts see
 * of the page.  The realm's global object becomes the window, with the
 * document, the console, the timers and queueMicrotask; each DOM node a
 * script reaches gets one object whose prototype chain follows the DOM's
 * interfaces (EventTarget, Node, Element, HTMLElement, Document, Text...);
 * and events are dispatched along the node tree with the capture, target
 * and bubble phases.
 *
 * The page drives time: it tells the window what time it is and asks it
 * to run the timers that are due, and a microtask checkpoint follows every
 * script, callback and event.
 */

#ifndef ZDESKTOP_BROWSER_BIND_H
#define ZDESKTOP_BROWSER_BIND_H

#include "dom/dom.h"
#include "js/js.h"

/*
 * The levels of the console's methods, which the host may show apart.
 */
enum bind_console_level {
	BIND_CONSOLE_LOG,
	BIND_CONSOLE_INFO,
	BIND_CONSOLE_WARN,
	BIND_CONSOLE_ERROR,
	BIND_CONSOLE_DEBUG
};

/*
 * The flags of bind_fire_event: the event bubbles; it can be canceled; its
 * target is the document although it is dispatched at the window (the
 * window's load event).
 */
#define BIND_EVENT_BUBBLES	0x1U
#define BIND_EVENT_CANCELABLE	0x2U
#define BIND_EVENT_DOCUMENT	0x4U

/*
 * What the window asks of its host: where the console's lines go (one
 * line of UTF-8 text at a level, without its line feed).
 */
struct bind_host {
	void *context;
	void (*console)(void *context, int level, const char *text, size_t length);
};

/*
 * A pointer event's place and button, for bind_fire_mouse_event: the
 * point in the viewport and in the document, in CSS pixels, and the DOM's
 * button number (0 is the main button).
 */
struct bind_mouse {
	double client_x;
	double client_y;
	double page_x;
	double page_y;
	int button;
};

struct bind_window;

/* The window (window.c). */
int bind_window_create(struct vm_realm *realm, struct dom_document *document, const struct bind_host *host, struct bind_window **window);
void bind_window_destroy(struct bind_window *window);
void bind_window_set_time(struct bind_window *window, double now);
void bind_window_set_viewport(struct bind_window *window, int width, int height);
void bind_window_set_ready_state(struct bind_window *window, const char *state);
int bind_run_script(struct bind_window *window, const uint16_t *source, size_t length, const char *name);
int bind_checkpoint(struct bind_window *window);
void bind_report_exception(struct bind_window *window, vm_value exception);
void bind_console(struct bind_window *window, int level, const char *text);

/* The timers (timer.c). */
int bind_next_timer(const struct bind_window *window, double *due);
int bind_run_timers(struct bind_window *window);

/* Events (event.c). */
int bind_fire_event(struct bind_window *window, struct dom_node *target, const char *type, unsigned flags, int *canceled);
int bind_fire_mouse_event(struct bind_window *window, struct dom_node *target, const char *type, const struct bind_mouse *mouse, int *canceled);

#endif
