/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the library's files share among themselves and do not export
 * (exports.map lets only the kl_ calls of <keiland-ui.h> out).
 */

#ifndef KEIUI_INTERNAL_H
#define KEIUI_INTERNAL_H

#include <keiland.h>

struct wl_display;
struct wl_registry;
struct wl_event_queue;
struct wl_interface;

/*
 * A search for one global of a display (globals.c, WS131 p015), for
 * libkeiland's objects: the global's name (0 when the compositor has none)
 * and version, the registry it is bound from -- an application's, when
 * the display is one (no search then), otherwise a registry of the
 * search's own on a queue of its own -- and the search's objects.
 */
struct keiui_global_search {
	uint32_t name;
	uint32_t version;
	struct wl_registry *registry;
	struct wl_event_queue *queue;
	struct wl_display *wrapper;
	struct wl_registry *own;
	const char *interface;
};

/* The theme handed out made the appearance's, and the light or the dark theme itself (theme.c, ws089-p017). */
void keiui_theme_set(unsigned appearance);
const struct kl_theme *keiui_theme_of(unsigned appearance);

/* Finds a global (0 or ENOMEM), binds it on the default queue (NULL when it cannot), and ends the search. */
int keiui_global_find(struct keiui_global_search *search, struct wl_display *display, const char *interface);
void *keiui_global_bind(struct keiui_global_search *search, const struct wl_interface *type, uint32_t version);
void keiui_global_end(struct keiui_global_search *search);

/* The registry of a display's application, with its global of an interface (*name 0 when none); NULL when the display is no application's (app.c). */
struct wl_registry *keiui_app_global(struct wl_display *display, const char *interface, uint32_t *name, uint32_t *version);

/*
 * What a widget's record says of it (ui.c): it takes the keyboard, it
 * takes a drag (a slider), it shuts out the widgets drawn before it from
 * Tab (a dialog).  A press on a record that does not take the keyboard
 * gives it to the record of the same id under it that does (a list's row:
 * the list).
 */
#define KEIUI_FOCUSABLE		1U
#define KEIUI_DRAGGABLE		2U
#define KEIUI_MODAL		4U

/* Records a widget with its flags and reports what the input did to it (ui.c, KL_HIT_* bits). */
unsigned keiui_ui_widget(struct kl_ui *ui, uint32_t id, uint32_t index, const struct kl_rect *rect, unsigned flags);

/* Tells whether a widget takes a key (its code and the modifiers held). */
typedef int (*keiui_wants_key)(uint32_t code, unsigned modifiers);

/* The index that stands for any of an id's widgets when keys are taken (a dialog's, a list's). */
#define KEIUI_ANY		0xfffffffeU

/* Takes the next key a widget wants among those pressed while it had the focus (ui.c); 1 with it, 0 when none is left for it (the others stay for the application). */
int keiui_ui_take_key(struct kl_ui *ui, uint32_t id, uint32_t index, keiui_wants_key wants, uint32_t *code, unsigned *modifiers);

/* What one input taken by a widget that edits text is (keiui_ui_take_input): none, a key, a text to commit, or bytes to delete around the caret. */
#define KEIUI_INPUT_NONE	0U
#define KEIUI_INPUT_KEY		1U
#define KEIUI_INPUT_COMMIT	2U
#define KEIUI_INPUT_DELETE	3U

/*
 * One input a widget that edits text takes (BUG-203): its kind, a key's
 * code and modifiers, the text an input method or the on-screen keyboard
 * commits, and the bytes to delete before and after the caret.
 */
struct keiui_input {
	unsigned kind;
	uint32_t code;
	unsigned modifiers;
	char text[KL_WINDOW_TEXT_MAX];
	uint32_t before;
	uint32_t after;
};

/* Takes the next key a widget wants, or text sent for it, in the order they came while it had the focus (ui.c); 1 with it, 0 when none is left for it. */
int keiui_ui_take_input(struct kl_ui *ui, uint32_t id, uint32_t index, keiui_wants_key wants, struct keiui_input *input);

/* Reports the text being composed for a widget, or NULL; a widget drawn without the focus drops it (ui.c). */
const char *keiui_ui_preedit(struct kl_ui *ui, uint32_t id, uint32_t index, int focused, int32_t *begin, int32_t *end);

/* Notes that the focused widget being drawn takes text from an input method, with its caret's rectangle in the window (ui.c). */
void keiui_ui_text_caret(struct kl_ui *ui, const struct kl_rect *caret);

/* Takes Enter and Space pressed while a widget had the focus (ui.c): 1 when one was pressed (the other keys stay for the application). */
int keiui_ui_take_activate(struct kl_ui *ui, uint32_t id, uint32_t index);

/* Reports the widget with the focus (ui.c): 1 with its id and index, 0 when none has it. */
int keiui_ui_focused(const struct kl_ui *ui, uint32_t *id, uint32_t *index);

/* Tells whether the focus's ring shows (ui.c): the keyboard moved the focus, not a click, a tap or the application. */
int keiui_ui_focus_ring(const struct kl_ui *ui);

/*
 * Notes the on-screen keyboard's inset a window heard (ui.c, from
 * window.c; ws102-p015): the window's size, the covered widths from its
 * right and bottom edges, the reason, and the caret's rectangle in the
 * window as the application last told it (kl_window_text_cursor; height 0
 * when it has not).  The next kl_ui_end of each window's input keeps its
 * text view's caret in sight (the library runs on one thread).
 */
void keiui_ui_inset_note(uint32_t width, uint32_t height, int right, int bottom, unsigned reason, const int32_t *caret);

/* Reports the time of the frame being drawn (ui.c, the time kl_ui_begin was given). */
uint64_t keiui_ui_now(const struct kl_ui *ui);

/* Draws a button that is one of several under an id (widgets.c; a dialog's) and reports 1 when it was pressed. */
int keiui_button(struct kl_ui *ui, const struct kl_style *style, uint32_t id, uint32_t index, const struct kl_rect *rect, const char *label, unsigned flags);

/* The line pictures, from KL_ICON_TILES on (icons-line.c). */
void keiui_icon_line_draw(struct kl_canvas *canvas, enum kl_icon icon, float x, float y, float size, kl_color color);

#endif
