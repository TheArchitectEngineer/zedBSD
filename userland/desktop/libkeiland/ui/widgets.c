/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The controls of the library (ws090-p005): the button, the switch and the
 * slider, in Settings' look (plan/ws089, settings/widgets.c: a 32-pixel
 * button with 8-pixel corners, the accent for the main one; a 44 by 24
 * switch whose knob moves to the accent's side).
 *
 * Each is drawn and takes its input in one call (plan/ws090/design.md
 * section 4): a click, a tap, and Enter or Space while it has the focus
 * press a button and flip a switch; a slider follows the pointer or the
 * finger that holds its knob, and the arrows, Page Up, Page Down, Home and
 * End while it has the focus.
 */

#include "internal.h"

#include <math.h>
#include <string.h>

/* A button's side margin, and its label's size. */
#define WIDGETS_BUTTON_SIDE	16
#define WIDGETS_TEXT_BUTTON	14U

/* How much darker a control is under the pointer, and how far a disabled one fades. */
#define WIDGETS_HOVER_SHADE	0.08f
#define WIDGETS_DISABLED_FADE	0.6f

/* The faded ground a disabled control mixes into. */
#define WIDGETS_FADED		kl_theme_choose(KL_RGB(0xeef1f5), KL_RGB(0x2a2e36))

/* The switch's knob: its radius and its distance from the track's ends. */
#define WIDGETS_KNOB_RADIUS	9.5f
#define WIDGETS_KNOB_INSET	12.0f

/* The slider's track thickness and its knob's radius. */
#define WIDGETS_TRACK		4.0f
#define WIDGETS_SLIDER_KNOB	9.0f

/* How many steps Page Up and Page Down move a slider. */
#define WIDGETS_SLIDER_PAGE	10.0

/* The focus ring's gap from the control and its width. */
#define WIDGETS_RING_GAP	3.0f
#define WIDGETS_RING		2.0f

static void widgets_ring(const struct kl_style *style, const struct kl_rect *rect, float radius);
static int widgets_slider_key(uint32_t code, unsigned modifiers);
static double widgets_snap(double value, double minimum, double maximum, double step);

/*
 * Reports how wide a button with a label is.
 */
int
kl_button_width(
	const struct kl_style *style,
	const char *label)
{
	int width;

	/* The label, bold, and a margin each side. */
	width = kl_text_width(style->text, label, strlen(label), WIDGETS_TEXT_BUTTON, 1);

	/* Reports the button's width. */
	return width + 2 * WIDGETS_BUTTON_SIDE;
}

/*
 * Draws a button (KL_BUTTON_* flags) and reports 1 when it was pressed
 * since the last frame.
 */
int
kl_button(
	struct kl_ui *ui,
	const struct kl_style *style,
	uint32_t id,
	const struct kl_rect *rect,
	const char *label,
	unsigned flags)
{
	int pressed;

	/* The button alone under its id. */
	pressed = keiui_button(ui, style, id, 0U, rect, label, flags);
	return pressed;
}

/*
 * Draws a button that is one of several under an id (a dialog's) and
 * reports 1 when it was pressed since the last frame.
 */
int
keiui_button(
	struct kl_ui *ui,
	const struct kl_style *style,
	uint32_t id,
	uint32_t index,
	const struct kl_rect *rect,
	const char *label,
	unsigned flags)
{
	const struct kl_theme *theme;
	kl_color ground;
	kl_color edge;
	kl_color ink;
	unsigned state;
	int enabled;
	int width;
	int pressed;
	int ring;

	/* The record: a working button takes the keyboard. */
	theme = style->theme;
	enabled = 1;
	if ((flags & KL_BUTTON_DISABLED) != 0U)
		enabled = 0;
	state = 0;
	if (enabled)
		state = keiui_ui_widget(ui, id, index, rect, KEIUI_FOCUSABLE);
	ring = keiui_ui_focus_ring(ui);

	/* Its colours: white with an edge, the accent for the main one, red for a dangerous one. */
	ground = theme->control;
	edge = theme->control_edge;
	ink = theme->text;
	if ((flags & KL_BUTTON_PRIMARY) != 0U) {
		ground = theme->accent;
		edge = theme->accent;
		ink = KL_RGB(0xffffff);
	}

	/* A dangerous one is red. */
	if ((flags & KL_BUTTON_DANGER) != 0U) {
		ground = theme->danger;
		edge = theme->danger;
		ink = KL_RGB(0xffffff);
	}

	/* Darker under the pointer or while held, faded when it does nothing. */
	if ((state & (KL_HIT_HOT | KL_HIT_ACTIVE)) != 0U)
		ground = kl_color_mix(ground, theme->text, WIDGETS_HOVER_SHADE);
	if (!enabled) {
		ground = kl_color_mix(ground, WIDGETS_FADED, WIDGETS_DISABLED_FADE);
		edge = kl_color_mix(edge, WIDGETS_FADED, WIDGETS_DISABLED_FADE);
		ink = theme->text_faint;
		if ((flags & (KL_BUTTON_PRIMARY | KL_BUTTON_DANGER)) != 0U)
			ink = KL_RGBA(0xffffff, 220);
	}

	/* The button, its label in the middle, and the focus's ring. */
	kl_canvas_round(style->canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, theme->control_radius, ground);
	kl_canvas_round_border(style->canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, theme->control_radius, 1.0f, edge);
	width = kl_text_width(style->text, label, strlen(label), WIDGETS_TEXT_BUTTON, 1);
	(void)kl_text_draw(style->text, style->canvas, rect->x + (rect->width - width) / 2, kl_text_center(WIDGETS_TEXT_BUTTON, rect->y, rect->height), label, strlen(label), WIDGETS_TEXT_BUTTON, 1, ink);
	if ((state & KL_HIT_FOCUSED) != 0U && ring)
		widgets_ring(style, rect, theme->control_radius);

	/* A disabled button is never pressed. */
	if (!enabled)
		return 0;

	/* Pressed: clicked, tapped, or Enter or Space while it has the focus. */
	pressed = 0;
	if ((state & KL_HIT_CLICKED) != 0U)
		pressed = 1;
	if ((state & KL_HIT_FOCUSED) != 0U)
		pressed |= keiui_ui_take_activate(ui, id, index);

	/* Reports whether it was pressed. */
	return pressed;
}

/*
 * Draws a switch with its top left at (x, y) (the theme's size) and flips
 * *on when it is pressed; reports 1 when it changed.  flags:
 * KL_BUTTON_DISABLED.
 */
int
kl_switch(
	struct kl_ui *ui,
	const struct kl_style *style,
	uint32_t id,
	int x,
	int y,
	int *on,
	unsigned flags)
{
	const struct kl_theme *theme;
	struct kl_rect rect;
	struct kl_rect reach;
	kl_color track;
	kl_color knob;
	unsigned state;
	float knob_x;
	int ring;
	int changed;
	int enabled;

	/* The record, a little larger than the switch for a finger. */
	theme = style->theme;
	rect.x = x;
	rect.y = y;
	rect.width = theme->switch_width;
	rect.height = theme->switch_height;
	reach.x = x - 4;
	reach.y = y - 4;
	reach.width = rect.width + 8;
	reach.height = rect.height + 8;
	enabled = 1;
	if ((flags & KL_BUTTON_DISABLED) != 0U)
		enabled = 0;
	state = 0;
	if (enabled)
		state = keiui_ui_widget(ui, id, 0U, &reach, KEIUI_FOCUSABLE);
	ring = keiui_ui_focus_ring(ui);

	/* Flipped: clicked, tapped, or Enter or Space while it has the focus. */
	changed = 0;
	if ((state & KL_HIT_CLICKED) != 0U)
		changed = 1;
	if ((state & KL_HIT_FOCUSED) != 0U)
		changed |= keiui_ui_take_activate(ui, id, 0U);

	/* The switch flips. */
	if (changed)
		*on = !*on;

	/* The track's colour and the knob's place. */
	track = theme->track;
	knob_x = (float)x + WIDGETS_KNOB_INSET;
	if (*on) {
		track = theme->accent;
		knob_x = (float)(x + rect.width) - WIDGETS_KNOB_INSET;
	}

	/* The knob is white; a switch that does nothing is faded. */
	knob = KL_RGB(0xffffff);
	if (!enabled) {
		track = kl_color_mix(track, WIDGETS_FADED, WIDGETS_DISABLED_FADE);
		knob = KL_RGB(0xf6f7f9);
	}

	/* The track, the knob and the focus's ring. */
	kl_canvas_round(style->canvas, (float)x, (float)y, (float)rect.width, (float)rect.height, (float)rect.height * 0.5f, track);
	kl_canvas_circle(style->canvas, knob_x, (float)y + (float)rect.height * 0.5f, WIDGETS_KNOB_RADIUS, knob);
	if ((state & KL_HIT_FOCUSED) != 0U && ring)
		widgets_ring(style, &rect, (float)rect.height * 0.5f);

	/* Reports whether it changed. */
	return changed;
}

/*
 * Draws a slider across a rectangle for *value within minimum..maximum
 * (on steps of step, none when 0) and moves the value with the input;
 * reports 1 when it changed.
 */
int
kl_slider(
	struct kl_ui *ui,
	const struct kl_style *style,
	uint32_t id,
	const struct kl_rect *rect,
	double minimum,
	double maximum,
	double step,
	double *value)
{
	const struct kl_theme *theme;
	uint32_t code;
	unsigned modifiers;
	unsigned state;
	double before;
	double share;
	double key_step;
	double pointer_x;
	double pointer_y;
	float left;
	float right;
	float middle;
	int ring;
	float knob;
	int taken;

	/* The record: it takes the keyboard and a drag. */
	theme = style->theme;
	state = keiui_ui_widget(ui, id, 0U, rect, KEIUI_FOCUSABLE | KEIUI_DRAGGABLE);
	ring = keiui_ui_focus_ring(ui);
	before = *value;
	left = (float)rect->x + WIDGETS_SLIDER_KNOB;
	right = (float)(rect->x + rect->width) - WIDGETS_SLIDER_KNOB;

	/* Held or clicked: the value where the pointer or the finger is along the track. */
	if ((state & (KL_HIT_ACTIVE | KL_HIT_CLICKED)) != 0U && right > left) {
		kl_ui_pointer(ui, &pointer_x, &pointer_y);
		share = ((double)pointer_x - (double)left) / (double)(right - left);
		*value = widgets_snap(minimum + share * (maximum - minimum), minimum, maximum, step);
	}

	/* The keys while it has the focus: a step, a page, the ends. */
	key_step = step;
	if (key_step <= 0.0)
		key_step = (maximum - minimum) / 100.0;
	for (;;) {
		taken = keiui_ui_take_key(ui, id, 0U, widgets_slider_key, &code, &modifiers);
		if (!taken)
			break;
		switch (code) {
		case KL_KEY_LEFT:
		case KL_KEY_DOWN:
			*value -= key_step;
			break;
		case KL_KEY_RIGHT:
		case KL_KEY_UP:
			*value += key_step;
			break;
		case KL_KEY_PAGEDOWN:
			*value -= key_step * WIDGETS_SLIDER_PAGE;
			break;
		case KL_KEY_PAGEUP:
			*value += key_step * WIDGETS_SLIDER_PAGE;
			break;
		case KL_KEY_HOME:
			*value = minimum;
			break;
		case KL_KEY_END:
			*value = maximum;
			break;
		default:
			break;
		}

		/* Kept on a step, inside the range. */
		*value = widgets_snap(*value, minimum, maximum, step);
	}

	/* The knob's place along the track. */
	share = 0.0;
	if (maximum > minimum)
		share = (*value - minimum) / (maximum - minimum);
	knob = left + (float)share * (right - left);
	middle = (float)rect->y + (float)rect->height * 0.5f;

	/* The track, the part up to the knob in the accent, the knob with a soft edge, and the focus's ring. */
	kl_canvas_round(style->canvas, left, middle - WIDGETS_TRACK * 0.5f, right - left, WIDGETS_TRACK, WIDGETS_TRACK * 0.5f, theme->track);
	kl_canvas_round(style->canvas, left, middle - WIDGETS_TRACK * 0.5f, knob - left, WIDGETS_TRACK, WIDGETS_TRACK * 0.5f, theme->accent);
	kl_canvas_circle(style->canvas, knob, middle + 1.0f, WIDGETS_SLIDER_KNOB + 1.0f, KL_RGBA(0x1f3a66, 40));
	kl_canvas_circle(style->canvas, knob, middle, WIDGETS_SLIDER_KNOB, KL_RGB(0xffffff));
	kl_canvas_ring(style->canvas, knob, middle, WIDGETS_SLIDER_KNOB, 1.0f, 1.0f, theme->control_edge);
	if ((state & KL_HIT_FOCUSED) != 0U && ring)
		widgets_ring(style, rect, (float)rect->height * 0.5f);

	/* Reports whether the value changed. */
	if (*value != before)
		return 1;
	return 0;
}

/* Draws the keyboard focus's ring around a control. */
static void
widgets_ring(
	const struct kl_style *style,
	const struct kl_rect *rect,
	float radius)
{
	/* A ring of the accent a little outside the control. */
	kl_canvas_round_border(style->canvas, (float)rect->x - WIDGETS_RING_GAP, (float)rect->y - WIDGETS_RING_GAP,
				(float)rect->width + 2.0f * WIDGETS_RING_GAP,
				(float)rect->height + 2.0f * WIDGETS_RING_GAP,
				radius + WIDGETS_RING_GAP,
				WIDGETS_RING,
				KL_RGBA(0x2f7cf6, 150));
}

/* Keeps a value within its ends and on its steps (none when step is 0). */
static double
widgets_snap(
	double value,
	double minimum,
	double maximum,
	double step)
{
	double steps;

	/* On a step from the minimum. */
	if (step > 0.0) {
		steps = floor((value - minimum) / step + 0.5);
		value = minimum + steps * step;
	}

	/* Within the ends. */
	if (value < minimum)
		value = minimum;
	if (value > maximum)
		value = maximum;

	/* Reports the value. */
	return value;
}

/* Tells whether a slider takes a key: the arrows, Page Up and Page Down, Home and End (without Control or Alt). */
static int
widgets_slider_key(
	uint32_t code,
	unsigned modifiers)
{
	/* A command is the application's. */
	if ((modifiers & (KL_MOD_CTRL | KL_MOD_ALT)) != 0U)
		return 0;

	/* The keys that move the value. */
	switch (code) {
	case KL_KEY_LEFT:
	case KL_KEY_RIGHT:
	case KL_KEY_UP:
	case KL_KEY_DOWN:
	case KL_KEY_PAGEUP:
	case KL_KEY_PAGEDOWN:
	case KL_KEY_HOME:
	case KL_KEY_END:
		return 1;
	default:
		break;
	}

	/* Any other. */
	return 0;
}
