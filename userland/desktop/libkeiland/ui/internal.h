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
