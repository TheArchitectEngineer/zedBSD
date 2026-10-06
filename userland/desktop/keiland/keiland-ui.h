/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's shared widgets and controls (WS090, plan/ws090/design.md):
 * one library the desktop's applications draw their parts with, so that a
 * button, a list or a scroll looks and feels the same in every one of them.
 *
 * The first layer (KUI_VERSION 1) is the drawing: a CPU canvas of
 * premultiplied BGRA pixels, the text drawn on it from TrueType fonts, the
 * icons made of its shapes, and the theme -- the colours and sizes of the
 * Kei look.  It began as the file manager's drawing surface (Files'
 * canvas.c, text.c and icons.c) and Settings' line pictures, moved here
 * unchanged so that an application moved onto the library draws the same
 * pixels as before.  The second (KUI_VERSION 2) is the scroll, the input
 * that finds which part of a frame a pointer, a wheel or a finger meant,
 * and the touch of a view of editable text.  The third (KUI_VERSION 3) is
 * the window: a Wayland toplevel whose input arrives as a queue of
 * events, whose CPU-drawn frames are shown through Vulkan or shared
 * memory, and which holds the clipboard and the primary selection.  The
 * fourth (KUI_VERSION 4) is the widgets: buttons, switches, sliders, text
 * fields, lists, sidebars, cards and their rows, dialogs, chips and
 * progress bars, drawn in the Kei look of Files and Settings, and the
 * keyboard's focus among them.
 *
 * Times are CLOCK_MONOTONIC microseconds throughout (the clock of
 * libkeiland's touch motion, scroller and gestures).
 *
 * Nothing in this layer knows about Wayland or Vulkan.  A window's frame
 * is drawn into a canvas and handed to its presenter, and host tests draw
 * into a canvas of their own and write it out as a picture.  Every call
 * is made from one thread.  The library's name is internal and never
 * appears in what a user reads.
 */

#ifndef KEILAND_UI_H
#define KEILAND_UI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif



/* How deep the clip rectangles nest. */
#define KL_CANVAS_CLIPS		16

/* The most corners a polygon may have. */
#define KL_POLYGON_POINTS	96

/*
 * How many fonts the text draws from: the main one, a fallback, and the
 * colour emoji font (KL_TEXT_EMOJI, opened the first time a character
 * neither of the others has is drawn; version 9).
 */
#define KL_TEXT_FACES		3
#define KL_TEXT_EMOJI		"/usr/share/fonts/keiland-emoji.ttf"

/* A color as 0xAARRGGBB, not premultiplied. */
typedef uint32_t kl_color;

/* An opaque color from 0xRRGGBB. */
#define KL_RGB(value)		((kl_color)(0xff000000U | (uint32_t)(value)))

/* A color from 0xRRGGBB and an alpha from 0 to 255. */
#define KL_RGBA(value, alpha)	((kl_color)(((uint32_t)(alpha) << 24) | ((uint32_t)(value) & 0xffffffU)))

/*
 * A rectangle of whole pixels.
 *
 * It is a plain value: a layout computes one, the drawing and the hit test
 * both read it.
 */
struct kl_rect {
	int x;
	int y;
	int width;
	int height;
};

/*
 * A picture in memory, premultiplied BGRA (0xAARRGGBB words).
 *
 * The pixels belong to whoever made the image: kl_image_create allocates
 * them and kl_image_release frees them.
 */
struct kl_image {
	uint32_t *pixels;
	int width;
	int height;
	size_t stride;
};

/*
 * A surface to draw on.
 *
 * The pixels are the caller's; the canvas adds the clip rectangles and the
 * scratch row the polygon filler accumulates coverage in, which live as
 * long as the canvas.
 */
struct kl_canvas {
	/* The pixels, the words in a row and the size. */
	uint32_t *pixels;
	size_t stride;
	int width;
	int height;

	/* The clip in force, and the ones it replaced (innermost last). */
	struct kl_rect clip;
	struct kl_rect clips[KL_CANVAS_CLIPS];
	int clip_depth;

	/* One row of polygon coverage, a float per pixel and one more. */
	float *coverage;
};

/*
 * One glyph drawn at one size, kept for the next time.
 *
 * key is zero for an empty slot; the bitmap is the glyph's coverage, or
 * pixels its colour (premultiplied 0xAARRGGBB, a colour emoji, version 9)
 * with bitmap NULL.
 */
struct kl_glyph {
	uint32_t key;
	int width;
	int height;
	int left;
	int top;
	int advance;
	uint8_t *bitmap;
	uint32_t *pixels;
};

/*
 * One font file: its bytes (kept for the face) and the face.
 */
struct kl_text_face {
	void *data;
	size_t size;
	struct truetype_face *face;
	unsigned pixels;
};

/*
 * The text of the window: the fonts and every glyph drawn so far.
 *
 * One lives for the whole program.  The cache is emptied when it fills up,
 * which only costs drawing the glyphs again.  emoji_tried says the emoji
 * font (faces[2]) was looked for (version 9).
 */
struct kl_text {
	struct kl_text_face faces[KL_TEXT_FACES];
	int face_count;
	int emoji_tried;
	struct kl_glyph *cache;
	unsigned cache_size;
	unsigned cache_used;
	uint8_t *scratch;
	size_t scratch_size;
};

/*
 * The vertical measurements of text at one size, in pixels.
 */
struct kl_text_line {
	int ascent;
	int descent;
	int height;
};

/*
 * The icons drawn with lines (sidebar, toolbar) or filled shapes (items).
 */
enum kl_icon {
	KL_ICON_HOME,
	KL_ICON_DESKTOP,
	KL_ICON_DOCUMENTS,
	KL_ICON_DOWNLOADS,
	KL_ICON_PICTURES,
	KL_ICON_MUSIC,
	KL_ICON_MOVIES,
	KL_ICON_FOLDER_LINE,
	KL_ICON_RECENTS,
	KL_ICON_TRASH,
	KL_ICON_COMPUTER,
	KL_ICON_VOLUME,
	KL_ICON_BACK,
	KL_ICON_FORWARD,
	KL_ICON_SEARCH,
	KL_ICON_GRID,
	KL_ICON_LIST,
	KL_ICON_PREVIEW,
	KL_ICON_CHEVRON,
	KL_ICON_CLOSE,
	KL_ICON_PLUS,
	KL_ICON_UP,
	KL_ICON_DOWN,

	/* The line pictures (Settings' pages), from KL_ICON_TILES on, in the order Settings numbered them. */
	KL_ICON_TILES,
	KL_ICON_WIFI,
	KL_ICON_ETHERNET,
	KL_ICON_BLUETOOTH,
	KL_ICON_SHIELD,
	KL_ICON_GLOBE,
	KL_ICON_PALETTE,
	KL_ICON_PICTURE,
	KL_ICON_BELL,
	KL_ICON_SPEAKER,
	KL_ICON_MONITOR,
	KL_ICON_DISK,
	KL_ICON_BATTERY,
	KL_ICON_KEYBOARD,
	KL_ICON_MOUSE,
	KL_ICON_TOUCHPAD,
	KL_ICON_PRINTER,
	KL_ICON_SHARE,
	KL_ICON_PEOPLE,
	KL_ICON_EYE,
	KL_ICON_LOCK,
	KL_ICON_PERSON,
	KL_ICON_REFRESH,
	KL_ICON_INFO,
	KL_ICON_DISCLOSURE
};

/* The canvas (canvas.c). */
int kl_canvas_init(struct kl_canvas *canvas, uint32_t *pixels, size_t stride, int width, int height);
void kl_canvas_release(struct kl_canvas *canvas);
void kl_canvas_clip_push(struct kl_canvas *canvas, const struct kl_rect *rect);
void kl_canvas_clip_pop(struct kl_canvas *canvas);
void kl_canvas_clear(struct kl_canvas *canvas);
void kl_canvas_fill(struct kl_canvas *canvas, const struct kl_rect *rect, kl_color color);
void kl_canvas_gradient(struct kl_canvas *canvas, const struct kl_rect *rect, kl_color top, kl_color bottom);
void kl_canvas_round(struct kl_canvas *canvas, float x, float y, float width, float height, float radius, kl_color color);
void kl_canvas_round_gradient(struct kl_canvas *canvas, float x, float y, float width, float height, float radius, kl_color top, kl_color bottom);
void kl_canvas_round_border(struct kl_canvas *canvas, float x, float y, float width, float height, float radius, float thickness, kl_color color);
void kl_canvas_shadow(struct kl_canvas *canvas, float x, float y, float width, float height, float radius, float softness, kl_color color);
void kl_canvas_circle(struct kl_canvas *canvas, float cx, float cy, float radius, kl_color color);
void kl_canvas_ring(struct kl_canvas *canvas, float cx, float cy, float radius, float thickness, float fraction, kl_color color);
void kl_canvas_polygon(struct kl_canvas *canvas, const float *points, int count, kl_color color);
void kl_canvas_line(struct kl_canvas *canvas, float x0, float y0, float x1, float y1, float thickness, kl_color color);
void kl_canvas_mask(struct kl_canvas *canvas, int x, int y, const uint8_t *mask, int width, int height, size_t stride, kl_color color);
void kl_canvas_image(struct kl_canvas *canvas, const struct kl_image *image, float x, float y, float width, float height, float radius, float opacity);
int kl_image_create(struct kl_image *image, int width, int height);
void kl_image_release(struct kl_image *image);
void kl_image_scale(const struct kl_image *source, struct kl_image *target);
kl_color kl_color_mix(kl_color from, kl_color to, float amount);

/* The text (text.c). */
int kl_text_open(struct kl_text *text, const char *primary, const char *fallback);
void kl_text_close(struct kl_text *text);
void kl_text_metrics(struct kl_text *text, unsigned pixels, struct kl_text_line *line);
int kl_text_center(unsigned pixels, int top, int height);
int kl_text_width(struct kl_text *text, const char *string, size_t length, unsigned pixels, int bold);
int kl_text_draw(struct kl_text *text, struct kl_canvas *canvas, int x, int baseline, const char *string, size_t length, unsigned pixels, int bold, kl_color color);
int kl_text_draw_fit(struct kl_text *text, struct kl_canvas *canvas, int x, int baseline, const char *string, unsigned pixels, int bold, int width, kl_color color);
size_t kl_text_fit(struct kl_text *text, const char *string, unsigned pixels, int bold, int width, char *out, size_t size);
size_t kl_text_break(struct kl_text *text, const char *string, unsigned pixels, int bold, int width);
uint32_t kl_utf8_next(const char *string, size_t length, size_t *index);

/*
 * The theme: the colours and sizes of the Kei look, which every widget
 * draws with (the file manager's values, plan/ws071/spec.md).  One theme
 * exists so far, the light one; an application reads it and does not
 * change it.
 */
struct kl_theme {
	/* The window's ground (a vertical gradient) and the cards on it. */
	kl_color ground_top;
	kl_color ground_bottom;
	kl_color panel;
	kl_color panel_edge;
	kl_color shadow;

	/* A sidebar and a content card standing on glass, and on the plain ground. */
	kl_color glass_sidebar;
	kl_color glass_content;
	kl_color sidebar;

	/* Text: the main ink, the secondary and the faint, and an icon's ink. */
	kl_color text;
	kl_color text_secondary;
	kl_color text_faint;
	kl_color icon;

	/* The accent, a selection with and without the keyboard, the pointer's hover, a separator, a folder and danger. */
	kl_color accent;
	kl_color selection;
	kl_color selection_inactive;
	kl_color hover;
	kl_color separator;
	kl_color folder;
	kl_color danger;

	/* A card's and a control's corner radius, a list row's height, and the text sizes of body, secondary and title text. */
	float card_radius;
	float control_radius;
	int row_height;
	unsigned text_body;
	unsigned text_small;
	unsigned text_title;

	/*
	 * KUI_VERSION 4 (Settings' values, plan/ws089): a card within a page
	 * and its edge, the line between a card's rows, a control's ground and
	 * edge, a switch's track when off, good and bad news, a control's
	 * height, and a switch's size.
	 */
	kl_color card;
	kl_color card_edge;
	kl_color row_separator;
	kl_color control;
	kl_color control_edge;
	kl_color track;
	kl_color good;
	kl_color bad;
	int control_height;
	int switch_width;
	int switch_height;
};

/* The theme (theme.c). */
const struct kl_theme *kl_theme_default(void);

/*
 * One of two colours by the desktop's appearance the program was told last
 * (KL_VERSION 35, ws089-p017): light in the light appearance, dark in the
 * dark one -- for a program's colours of its own beside the theme's.
 */
kl_color kl_theme_choose(kl_color light, kl_color dark);

/* The icons (icons.c and icons-line.c). */
void kl_icon_draw(struct kl_canvas *canvas, enum kl_icon icon, float x, float y, float size, kl_color color);
void kl_icon_folder(struct kl_canvas *canvas, float x, float y, float size, kl_color tint);
void kl_icon_file(struct kl_canvas *canvas, struct kl_text *text, float x, float y, float size, kl_color band, const char *label);
void kl_icon_tag(struct kl_canvas *canvas, float cx, float cy, float radius, kl_color color);

/*
 * The scroll (scroll.c, KUI_VERSION 2): the state of one part of a window
 * whose content is larger than the part, which an application keeps and
 * draws its content at (x, y) of.
 *
 * The wheel and the keys glide the content to where they send it (the
 * distance left shrinks by e every KL_SCROLL_GLIDE_US); a finger drags it
 * and lets it fly on with libkeiland's scroller (the inertia and the
 * rubber band past an end are the scroller's).  Outside a finger's hold
 * the position stays within 0..content-viewport on each axis that
 * scrolls.  The scroll bars show while the content moves and fade out
 * over KL_SCROLL_FADE_US after it stops.
 *
 * The fields are read by the application (x and y above all); they are
 * written only through the calls.  Nothing here draws but
 * kl_scroll_draw_bars, so a program that draws its own content with
 * Vulkan (Terminal, Notes) uses the same scroll without the canvas.
 */
struct kl_scroller;

/* The axes a scroll moves along. */
#define KL_SCROLL_X		1U
#define KL_SCROLL_Y		2U

/* How quickly a glide closes on its target (the time constant), and how long the bars take to fade. */
#define KL_SCROLL_GLIDE_US	70000U
#define KL_SCROLL_FADE_US	1000000U

/*
 * The track of a touch pad's two-finger scrolling (KL_VERSION 40, BUG-211):
 * its last moves and their times, from which the velocity is worked out
 * when the fingers lift, so that the content flies on.  The samples older
 * than KL_AXIS_TRACK_WINDOW_US at the lift do not count, and fingers that
 * rested longer than KL_AXIS_TRACK_REST_US before lifting throw nothing.
 * It is plain data a caller keeps; kl_scroller keeps one for a touch pad's
 * fingers (KL_VERSION 41), so a program needs none of its own.
 */
#define KL_AXIS_TRACK_SAMPLES	16U
#define KL_AXIS_TRACK_WINDOW_US	100000U
#define KL_AXIS_TRACK_REST_US	60000U

struct kl_axis_track {
	unsigned count;
	unsigned next;
	double dx[KL_AXIS_TRACK_SAMPLES];
	double dy[KL_AXIS_TRACK_SAMPLES];
	uint64_t us[KL_AXIS_TRACK_SAMPLES];
};

void kl_axis_track_reset(struct kl_axis_track *track);
void kl_axis_track_add(struct kl_axis_track *track, double dx, double dy, uint64_t now_us);
void kl_axis_track_velocity(const struct kl_axis_track *track, uint64_t now_us, double *vx, double *vy);

struct kl_scroll {
	/* The axes it moves along, the position drawn, and the sizes of the content and of the part that shows it. */
	unsigned axes;
	double x;
	double y;
	double content_width;
	double content_height;
	double viewport_width;
	double viewport_height;

	/* A glide of the wheel or the keys: where it started, where it goes, and when it started. */
	int gliding;
	double from_x;
	double from_y;
	double to_x;
	double to_y;
	uint64_t glide_us;

	/* The finger's scroller, whether it owns the content (a finger holds it or it flies on), and whether the finger has lifted. */
	struct kl_scroller *scroller;
	int touched;
	int released;

	/* When the content last moved (for the bars), 0 before it ever moved. */
	uint64_t moved_us;
};

int kl_scroll_init(struct kl_scroll *scroll, unsigned axes);
void kl_scroll_release(struct kl_scroll *scroll);
void kl_scroll_set_size(struct kl_scroll *scroll, double content_width, double content_height, double viewport_width, double viewport_height);
void kl_scroll_wheel(struct kl_scroll *scroll, double dx, double dy, uint64_t now_us);
void kl_scroll_move_to(struct kl_scroll *scroll, double x, double y, int glide, uint64_t now_us);
void kl_scroll_reveal(struct kl_scroll *scroll, const struct kl_rect *rect, uint64_t now_us);
int kl_scroll_key(struct kl_scroll *scroll, uint32_t key, unsigned modifiers, double line, uint64_t now_us);
int kl_scroll_press(struct kl_scroll *scroll, uint64_t now_us);
void kl_scroll_drag(struct kl_scroll *scroll, double dx, double dy);
void kl_scroll_fling(struct kl_scroll *scroll, double vx, double vy, uint64_t now_us);
void kl_scroll_cancel(struct kl_scroll *scroll, uint64_t now_us);
void kl_scroll_axis(struct kl_scroll *scroll, double dx, double dy, unsigned source, uint64_t now_us);
int kl_scroll_axis_stop(struct kl_scroll *scroll, uint64_t now_us);
int kl_scroll_step(struct kl_scroll *scroll, uint64_t now_us);
double kl_scroll_limit_x(const struct kl_scroll *scroll);
double kl_scroll_limit_y(const struct kl_scroll *scroll);
int kl_scroll_draw_bars(const struct kl_scroll *scroll, struct kl_canvas *canvas, const struct kl_rect *viewport, const struct kl_theme *theme, uint64_t now_us);

/*
 * The overlay scroll bar (scroll-bar.c, KUI_VERSION 12, ws127-p002): the
 * vertical bar of a view, drawn over its content's right edge the way
 * macOS draws one (the user's choice of 2026-10-02).  It comes out thin
 * while the content moves, grows thick (with a faint track) while the
 * pointer is near the edge or drags it, and fades a while after the last
 * of these.  A press on the thumb drags the content; a press on the track
 * moves it a page towards the press.
 *
 * The state knows nothing of the window or of drawing: the application
 * tells it what happened (its sizes are in the content's pixels, offset
 * is how far the content is scrolled), asks for the shape to draw with its
 * own canvas (kl_scroll_bar_draw draws it on a kl_canvas), and draws
 * again while kl_scroll_bar_busy says the bar still changes.  The times
 * are microseconds of one clock.
 */
#define KL_SCROLL_BAR_THIN	6
#define KL_SCROLL_BAR_THICK	11
#define KL_SCROLL_BAR_REACH	16
#define KL_SCROLL_BAR_GAP	2
#define KL_SCROLL_BAR_MIN	28
#define KL_SCROLL_BAR_SHOW_US	1000000U
#define KL_SCROLL_BAR_FADE_US	400000U

/*
 * The state of one overlay bar.  active_us is when the content last moved
 * or the pointer last came near or dragged (0 before any); near says the
 * pointer is over the bar's band; dragging and grab (where in the thumb the
 * drag holds it) belong to a press on the thumb.  Zeroed, it is a bar that
 * has not shown yet.
 */
struct kl_scroll_bar {
	uint64_t active_us;
	int near;
	int dragging;
	double grab;
};

/*
 * What to draw now: the track (shown while the bar is thick) and the
 * thumb, in the window's pixels, and the strength of the ink (0 to 1).
 */
struct kl_scroll_bar_shape {
	double track_x;
	double track_y;
	double track_width;
	double track_height;
	double thumb_x;
	double thumb_y;
	double thumb_width;
	double thumb_height;
	double alpha;
	int thick;
};

void kl_scroll_bar_moved(struct kl_scroll_bar *bar, uint64_t now_us);
int kl_scroll_bar_hover(struct kl_scroll_bar *bar, const struct kl_rect *viewport, double content, double x, double y, uint64_t now_us);
int kl_scroll_bar_leave(struct kl_scroll_bar *bar, uint64_t now_us);
int kl_scroll_bar_shape(const struct kl_scroll_bar *bar, const struct kl_rect *viewport, double content, double offset, uint64_t now_us, struct kl_scroll_bar_shape *shape);
int kl_scroll_bar_press(struct kl_scroll_bar *bar, const struct kl_rect *viewport, double content, double offset, double x, double y, uint64_t now_us, double *new_offset);
int kl_scroll_bar_drag(struct kl_scroll_bar *bar, const struct kl_rect *viewport, double content, double y, uint64_t now_us, double *new_offset);
int kl_scroll_bar_release(struct kl_scroll_bar *bar, uint64_t now_us);
int kl_scroll_bar_busy(const struct kl_scroll_bar *bar, uint64_t now_us);
int kl_scroll_bar_draw(const struct kl_scroll_bar *bar, struct kl_canvas *canvas, const struct kl_rect *viewport, double content, double offset, uint64_t now_us);

/*
 * The keys (input.c, KUI_VERSION 2).  zdesktop forwards evdev key codes
 * with no keymap; the library carries the US layout, as the desktop's
 * programs do, until an input method arrives (WS095).
 */
#define KL_KEY_ESC		1U
#define KL_KEY_BACKSPACE	14U
#define KL_KEY_TAB		15U
#define KL_KEY_ENTER		28U
#define KL_KEY_SPACE		57U
#define KL_KEY_KPENTER	96U
#define KL_KEY_HOME		102U
#define KL_KEY_UP		103U
#define KL_KEY_PAGEUP		104U
#define KL_KEY_LEFT		105U
#define KL_KEY_RIGHT		106U
#define KL_KEY_END		107U
#define KL_KEY_DOWN		108U
#define KL_KEY_PAGEDOWN	109U
#define KL_KEY_DELETE		111U

/* The modifiers held. */
#define KL_MOD_SHIFT		1U
#define KL_MOD_CTRL		2U
#define KL_MOD_ALT		4U
#define KL_MOD_SUPER		8U

uint32_t kl_key_character(uint32_t key, unsigned modifiers);

/*
 * The touch of a view of editable text (text-touch.c, KUI_VERSION 2,
 * plan/ws090/design.md section 6.1): in a text editor's body and a text
 * field, one finger's drag selects and two fingers scroll.
 *
 * A tap puts the caret, a double tap selects a word, one finger's drag
 * selects from where it touched (the content scrolls by itself while the
 * finger is near the view's edge), and a long press asks for the context
 * menu.  A selection made by touch shows a handle at each end; dragging a
 * handle moves that end.  Two fingers are the scroll's (kl_ui gives them
 * to it).
 *
 * The view gives three answers in its content's coordinates (the scroll
 * already undone): the text position nearest a point, the caret's
 * rectangle at a position, and the word around a position.  Positions are
 * whatever the view counts in (byte offsets in Text Editor).
 */
struct kl_text_view {
	size_t (*position_at)(void *data, double x, double y);
	void (*caret_rect)(void *data, size_t position, struct kl_rect *rect);
	void (*word_at)(void *data, size_t position, size_t *start, size_t *end);
};

/* What the fingers changed, for kl_text_touch_take. */
#define KL_TEXT_TOUCH_SELECTION	1U
#define KL_TEXT_TOUCH_MENU		2U

/* The handles: their drawn diameter and the diameter a finger finds them within. */
#define KL_TEXT_HANDLE		12
#define KL_TEXT_HANDLE_REACH	44

/* How near the view's edge a selecting finger scrolls the content, and how fast at the edge (pixels a second). */
#define KL_TEXT_EDGE		24
#define KL_TEXT_EDGE_SPEED	1200.0

/* Which end of the selection a handle drag moves. */
#define KL_TEXT_HANDLE_NONE	0
#define KL_TEXT_HANDLE_ANCHOR	1
#define KL_TEXT_HANDLE_CARET	2

struct kl_text_touch {
	/* The view's answers and their data. */
	const struct kl_text_view *view;
	void *data;

	/* The selection: from the anchor to the caret (equal: only a caret). */
	size_t anchor;
	size_t caret;

	/*
	 * Whether a finger is selecting, which handle it holds, whether the
	 * handles show, the finger (content coordinates), and how far the finger
	 * holding a handle is from the middle of its end's caret (subtracted, so
	 * that the end follows the caret's line, not the knob's below it).
	 */
	int selecting;
	int handle;
	int handles;
	double finger_x;
	double finger_y;
	double grip_x;
	double grip_y;

	/* What changed since kl_text_touch_take, and where the context menu was asked for (window coordinates). */
	unsigned changes;
	double menu_x;
	double menu_y;
};

void kl_text_touch_init(struct kl_text_touch *touch, const struct kl_text_view *view, void *data);
void kl_text_touch_set_selection(struct kl_text_touch *touch, size_t anchor, size_t caret);
void kl_text_touch_tap(struct kl_text_touch *touch, double x, double y, int twice);
void kl_text_touch_long_press(struct kl_text_touch *touch, double window_x, double window_y);
void kl_text_touch_drag_begin(struct kl_text_touch *touch, double x, double y);
void kl_text_touch_drag(struct kl_text_touch *touch, double x, double y);
void kl_text_touch_drag_end(struct kl_text_touch *touch);
int kl_text_touch_edge(const struct kl_text_touch *touch, const struct kl_scroll *scroll, double *vx, double *vy);
unsigned kl_text_touch_take(struct kl_text_touch *touch);
void kl_text_touch_draw_handles(const struct kl_text_touch *touch, struct kl_canvas *canvas, double origin_x, double origin_y, const struct kl_theme *theme);

/*
 * The input of a window (ui.c, KUI_VERSION 2, design section 4): which
 * part of a frame a pointer, the wheel or a finger meant.
 *
 * While a frame is drawn, each part that takes input is recorded: a
 * widget (kl_ui_hit, by an id the application chooses and an index), a
 * scroll's viewport (kl_ui_scroll_region) and a view of editable text
 * (kl_ui_text_region).  Input that arrives before the next frame is
 * resolved against the parts of the frame last drawn, the latest recorded
 * first (on top): a press and a release on the same widget click it (twice
 * within 400 ms: a double click), the wheel goes to the scroll under the
 * pointer, and a finger goes by libkeiland's gestures: a tap to the widget
 * it touched (else to the text view there), a drag to the scroll or text
 * view it touched (a widget such as a list's row inside a scroll does not
 * take a drag; the scroll does).  In a text view one finger's drag selects
 * and two fingers' drag scrolls (kl_text_touch).  Input that meets no
 * part is kept for the application (kl_ui_take; a drag's distance from
 * kl_ui_drag_offset).  Each input call returns 1 when the window must
 * draw again.
 *
 * A scroll or a text touch given to kl_ui_scroll_region or
 * kl_ui_text_region must live until the next frame is drawn (the input
 * in between reaches it).
 */
struct kl_ui;

/* What a widget's record reports of the input (bits). */
#define KL_HIT_HOT		1U	/* the pointer is over it */
#define KL_HIT_ACTIVE		2U	/* a press on it is held */
#define KL_HIT_CLICKED	4U	/* pressed and released on it since the last frame */
#define KL_HIT_DOUBLE		8U	/* the click was the second of a double click or tap */
#define KL_HIT_FOCUSED	16U	/* it has the keyboard's focus (a widget that takes the keyboard) */
#define KL_HIT_TOUCHED	32U	/* KUI_VERSION 5: the click was a finger's tap */

/* The input no part took. */
#define KL_EVENT_PRESS		1U
#define KL_EVENT_RELEASE	2U
#define KL_EVENT_WHEEL		3U
#define KL_EVENT_TAP		4U
#define KL_EVENT_DOUBLE_TAP	5U
#define KL_EVENT_LONG_PRESS	6U
#define KL_EVENT_DRAG_BEGIN	7U
#define KL_EVENT_DRAG_END	8U

/*
 * One input no part took: its kind, where (window coordinates), the
 * wheel's distance or a drag's velocity, the fingers down, and the id of
 * the region it happened over (a long press over a text view: that view's
 * id, 0 over none).
 */
struct kl_event {
	unsigned kind;
	double x;
	double y;
	double dx;
	double dy;
	unsigned fingers;
	uint32_t region;
	uint32_t code;
	unsigned modifiers;
};

struct kl_window_event;

struct kl_ui *kl_ui_create(void);
void kl_ui_destroy(struct kl_ui *ui);
int kl_ui_pointer_motion(struct kl_ui *ui, double x, double y);
int kl_ui_pointer_leave(struct kl_ui *ui);
int kl_ui_pointer_button(struct kl_ui *ui, int pressed, uint64_t now_us);
int kl_ui_wheel(struct kl_ui *ui, double dx, double dy, uint64_t now_us);
/*
 * Takes a window's scrolling (KL_WINDOW_AXIS, KL_WINDOW_AXIS_STOP): 0 when
 * nothing took it, 1 when a scroll did, KL_UI_AXIS_FLUNG (KL_VERSION 43)
 * when the fingers' lift threw the content (it flies on).
 */
#define KL_UI_AXIS_FLUNG	2
int kl_ui_axis(struct kl_ui *ui, const struct kl_window_event *event);
int kl_ui_touch_down(struct kl_ui *ui, int32_t id, uint64_t time_us, uint64_t now_us, double x, double y);
int kl_ui_touch_motion(struct kl_ui *ui, int32_t id, uint64_t time_us, uint64_t now_us, double x, double y);
int kl_ui_touch_up(struct kl_ui *ui, int32_t id, uint64_t time_us, uint64_t now_us);
int kl_ui_touch_cancel(struct kl_ui *ui, uint64_t now_us);
void kl_ui_begin(struct kl_ui *ui, uint64_t now_us);
unsigned kl_ui_hit(struct kl_ui *ui, uint32_t id, uint32_t index, const struct kl_rect *rect);
void kl_ui_scroll_region(struct kl_ui *ui, uint32_t id, const struct kl_rect *rect, struct kl_scroll *scroll);
void kl_ui_text_region(struct kl_ui *ui, uint32_t id, const struct kl_rect *rect, struct kl_scroll *scroll, struct kl_text_touch *touch);
int kl_ui_end(struct kl_ui *ui, uint64_t now_us);
int kl_ui_take(struct kl_ui *ui, struct kl_event *event);
int kl_ui_drag_offset(struct kl_ui *ui, uint64_t now_us, double *dx, double *dy);

/*
 * The window (window.c, present.c, present-shm.c, clipboard.c,
 * primary.c; KUI_VERSION 3, plan/ws090/design.md section 5): an
 * xdg-shell toplevel of its own connection, its seat's input, the frames
 * the application draws on the CPU, and the clipboard and the primary
 * selection.
 *
 * The input arrives as events in a queue the application takes after
 * each kl_window_dispatch, in the order they came: the pointer, the
 * wheel, the keys (a held key repeats: the application calls
 * kl_window_repeat after the dispatch, so that a release read in the
 * same dispatch stops it first, BUG-111), the keyboard's focus, the
 * fingers (their times turned into CLOCK_MONOTONIC microseconds), a new
 * size and the request to close.  An application posts its own inputs
 * heard through other objects during a dispatch (a System Menu's shortcut,
 * a titlebar's control) with kl_window_post, so that they keep their
 * place among the keys (typed text, then Ctrl+S).  Input on the program's other surfaces
 * (a file chooser's window) is not the window's and never queued.
 *
 * A frame is ordinary memory of premultiplied 0xAARRGGBB words the size
 * kl_window_present_resize reported.  KL_PRESENT_VULKAN shows it through
 * a Vulkan swapchain (see-through when the compositor offers it, the way
 * zdesktop's glass needs), KL_PRESENT_SHM through wl_shm buffers (for a
 * small window of a library, or where Vulkan is missing), and
 * KL_PRESENT_NONE leaves the surface to the application's own Vulkan.
 * The menus, the titlebar's controls and the glass panels stay the
 * application's (libkeiland), on the objects the accessors give.
 */
struct kl_window;
struct wl_display;
struct wl_surface;
struct wl_seat;
struct xdg_toplevel;

/* How the frames are shown. */
#define KL_PRESENT_VULKAN	0U
#define KL_PRESENT_SHM		1U
#define KL_PRESENT_NONE	2U

/* The kinds of input. */
#define KL_WINDOW_MOTION	1U
#define KL_WINDOW_LEAVE	2U
#define KL_WINDOW_BUTTON	3U
#define KL_WINDOW_AXIS		4U
#define KL_WINDOW_KEY		5U
#define KL_WINDOW_FOCUS	6U
#define KL_WINDOW_TOUCH_DOWN	7U
#define KL_WINDOW_TOUCH_MOTION	8U
#define KL_WINDOW_TOUCH_UP	9U
#define KL_WINDOW_TOUCH_CANCEL	10U
#define KL_WINDOW_RESIZE	11U
#define KL_WINDOW_CLOSE	12U
#define KL_WINDOW_POST		13U

/*
 * KUI_VERSION 6: the text an input method or zdesktop's on-screen keyboard
 * sends through the text input (text-input-unstable-v3), while the window
 * asks for it (kl_window_text_input): text to insert at the caret in place
 * of the selection (text), the text being composed to show at the caret
 * until it is committed or replaced (text, empty when it goes; begin and end
 * are its cursor's byte offsets, -1 when hidden), and bytes to delete
 * before and after the caret first (before, after).  They come in the order
 * of the protocol's done: delete, commit, preedit.
 */
#define KL_WINDOW_TEXT_COMMIT	14U
#define KL_WINDOW_TEXT_PREEDIT	15U
#define KL_WINDOW_TEXT_DELETE	16U

/*
 * KL_VERSION 40 (BUG-211): what a KL_WINDOW_AXIS came from (axis_source:
 * a wheel, a touch pad's fingers, or something continuous), and the end of
 * the fingers' scrolling, KL_WINDOW_AXIS_STOP, after which the content may
 * fly on (kl_ui_axis, kl_scroll_axis_stop).  It is 18 (KL_VERSION 43):
 * KL_VERSION 40 gave it 17, the number KL_WINDOW_ACTION has had since
 * KL_VERSION 26, so that a lift of the fingers came to an application of
 * kl_app as an action.
 */
#define KL_WINDOW_AXIS_STOP	18U
#define KL_AXIS_SOURCE_WHEEL		0U
#define KL_AXIS_SOURCE_FINGER		1U
#define KL_AXIS_SOURCE_CONTINUOUS	2U

/* The longest text one input carries, with its NUL (a longer one is cut at a character's start). */
#define KL_WINDOW_TEXT_MAX	256U

/* The evdev codes of the pointer's buttons. */
#define KL_BUTTON_LEFT		0x110U
#define KL_BUTTON_RIGHT	0x111U
#define KL_BUTTON_MIDDLE	0x112U

/*
 * What a window is made with.  Any pointer may be NULL: display (the
 * WAYLAND_DISPLAY one), title and application (the app_id).  width and
 * height are the size asked for until the compositor gives one.
 * fullscreen (KUI_VERSION 11) asks for the full screen before the window
 * is first configured, so that its first configure is the full screen's.
 */
struct kl_window_options {
	const char *display;
	const char *title;
	const char *application;
	uint32_t width;
	uint32_t height;
	unsigned present;
	int fullscreen;
};

/*
 * One input: its kind (KL_WINDOW_*), where the pointer or the finger is
 * (surface pixels), a button's or a key's code and whether it is pressed
 * (a focus: 1 when it came), whether a key is a repeat, the modifiers held
 * (KL_MOD_*), the wheel's distance in pixels, a finger's id and the time
 * it happened (a finger's, and since KUI_VERSION 11 the pointer's motions and
 * buttons, from the compositor's time; otherwise when it was read), when it
 * was read, and its serial (a press's, for a popup or
 * a selection); for the text input's (KUI_VERSION 6), its text, the
 * composed text's cursor, and the bytes to delete around the caret; for a
 * pen tablet's (KL_VERSION 44), its tool (KL_TABLET_*), its barrel buttons
 * held (KL_TABLET_BUTTON_*), its pressure (0 to 1, -1 for a tool without
 * it) and its tilt (degrees).
 */
struct kl_window_event {
	unsigned kind;
	double x;
	double y;
	uint32_t code;
	int pressed;
	int repeated;
	unsigned modifiers;
	double dx;
	double dy;
	int32_t id;
	uint64_t time_us;
	uint64_t arrival_us;
	uint32_t serial;
	char text[KL_WINDOW_TEXT_MAX];
	int32_t begin;
	int32_t end;
	uint32_t before;
	uint32_t after;
	unsigned axis_source;
	unsigned tool;
	unsigned buttons;
	double pressure;
	double tilt_x;
	double tilt_y;
};

struct kl_window *kl_window_open(const struct kl_window_options *options);
void kl_window_close(struct kl_window *window);
int kl_window_dispatch(struct kl_window *window, int timeout_ms);
int kl_window_take(struct kl_window *window, struct kl_window_event *event);
void kl_window_post(struct kl_window *window, uint32_t code);
int kl_window_repeat(struct kl_window *window, uint64_t now_us);
int kl_window_repeat_wait(const struct kl_window *window, uint64_t now_us);
void kl_window_set_title(struct kl_window *window, const char *title);
void kl_window_size(const struct kl_window *window, uint32_t *width, uint32_t *height);
int kl_window_present_resize(struct kl_window *window, uint32_t *width, uint32_t *height);
int kl_window_present(struct kl_window *window, const uint32_t *pixels, size_t stride);
int kl_window_see_through(const struct kl_window *window);
struct wl_display *kl_window_display(const struct kl_window *window);
struct wl_surface *kl_window_surface(const struct kl_window *window);
struct xdg_toplevel *kl_window_toplevel(const struct kl_window *window);
uint32_t kl_window_serial(const struct kl_window *window);
uint32_t kl_window_press_serial(const struct kl_window *window);
void kl_window_set_serial(struct kl_window *window, uint32_t serial);
struct wl_seat *kl_window_seat(const struct kl_window *window);
void kl_window_copy(struct kl_window *window, const char *text, size_t length);
size_t kl_window_paste(struct kl_window *window, char *text, size_t size);
int kl_window_can_paste(const struct kl_window *window);
void kl_window_text_input(struct kl_window *window, int enabled);
void kl_window_text_cursor(struct kl_window *window, int x, int y, int width, int height);
void kl_window_select(struct kl_window *window, const char *text, size_t length);

/*
 * KUI_VERSION 7 (ws102-p015, plan/ws102/design.md section 2.8): the
 * on-screen keyboard's inset.  zdesktop tells a window how much of it the
 * keyboard covers, in the window's pixels from its right edge (the flick
 * panel's column) and from its bottom edge (the QWERTY row), when the
 * keyboard opens, closes or changes the window (before that configure);
 * both are 0 when it has closed or does not cover the window.  With a
 * compositor that does not tell, nothing happens.
 *
 * By default the caret is kept in sight: when the keyboard comes or
 * changes, the next kl_ui_end moves the scroll of the frame's text view
 * (kl_ui_text_region; the one with the keyboard's focus, else the last
 * recorded) so that the caret's line is in the middle of the part of the
 * view the keyboard leaves, as far as the scroll goes (not past the text's
 * start or end; a view that does not scroll stays).  The application may
 * hear the inset first: its callback returns 1 when it took care of it
 * itself (the default is skipped), 0 to keep the default.  The reasons are
 * KL_KEYBOARD_INSET_* of <keiland.h> (one definition since WS131 p014).
 */
typedef int (*kl_window_keyboard_inset_fn)(void *data, int right, int bottom, unsigned reason);
void kl_window_on_keyboard_inset(struct kl_window *window, kl_window_keyboard_inset_fn callback, void *data);
void kl_window_keyboard_inset(const struct kl_window *window, int *right, int *bottom);

/*
 * KUI_VERSION 8 (ws102-p017, plan/ws102/design.md section 2.10): the
 * editing operations the on-screen keyboard's buttons ask for.  A window
 * tells zdesktop it carries all of them out and its state, and hears them.
 * By default each becomes the keys it stands for, queued as the window's
 * own key inputs (copy Ctrl+C, cut Ctrl+X, paste Ctrl+V, undo Ctrl+Z, redo
 * Ctrl+Shift+Z, select all Ctrl+A), and select_begin and select_end start
 * and end a selection: while it is made, the keys that move the caret
 * (the arrows, Home, End, Page Up and Page Down) come with Shift, and a
 * copy or a cut ends it.  An application that knows its state tells it
 * (kl_window_edit_state: KL_EDIT_HAS_SELECTION ...); otherwise a
 * selection, undo and redo are taken to be there and paste follows the
 * clipboard.  The application may hear an operation first: its callback
 * returns 1 when it carried it out itself (the default is skipped).  The
 * operations and the state's bits are KL_EDIT_* of <keiland.h> (one
 * definition since WS131 p014).
 */
typedef int (*kl_window_edit_fn)(void *data, unsigned operation);
void kl_window_on_edit(struct kl_window *window, kl_window_edit_fn callback, void *data);
void kl_window_edit_state(struct kl_window *window, unsigned state);
int kl_window_selecting(const struct kl_window *window);
size_t kl_window_paste_primary(struct kl_window *window, char *text, size_t size);

/*
 * KUI_VERSION 10 (ws090-p008, Image Viewer): the full screen.  An
 * application asks the compositor for it (or out of it), and learns from
 * the configure whether the window is fullscreen.
 */
void kl_window_set_fullscreen(struct kl_window *window, int fullscreen);
int kl_window_fullscreen(const struct kl_window *window);

/*
 * KL_VERSION 42 (ws122-p005b): what the window shows, told to the
 * compositor (wp_content_type_v1) from the window's next frame: nothing in
 * particular, a photo, a video or a game.  A fullscreen video or game may
 * then be shown without composing (the compositor's game mode).  Without
 * the compositor's protocol nothing is told (ENOTSUP).
 */
#define KL_CONTENT_NONE		0U
#define KL_CONTENT_PHOTO	1U
#define KL_CONTENT_VIDEO	2U
#define KL_CONTENT_GAME		3U
int kl_window_set_content_type(struct kl_window *window, unsigned type);

/*
 * KUI_VERSION 11 (ws090-p011, Terminal): an application that waits for
 * other descriptors too (a terminal's shells) waits for them with the
 * compositor, at most KL_WINDOW_FDS_MAX of them; ready[i] says fds[i] has
 * something to read or has hung up.
 */
#define KL_WINDOW_FDS_MAX	16U
int kl_window_dispatch_fds(struct kl_window *window, const int *fds, unsigned count, int timeout_ms, int *ready);
uint64_t kl_clock_us(void);

/*
 * KL_VERSION 26 (WS131 p015, plan/ws131/design.md section 6): the
 * application.  One kl_app is one connection to the compositor: its
 * globals are learnt with one roundtrip when it opens, and its windows,
 * menus, titlebars and glass bind what they need from that one registry
 * without searching again.  Its windows' input, the actions chosen in their
 * menus and controls, and the descriptors it watches arrive as one queue of
 * kl_app_event values, in the order they happened: the application waits
 * with kl_app_dispatch and takes them with kl_app_take (a window of an
 * application has no queue of its own; kl_window_take finds nothing).  A
 * held key repeats by itself within kl_app_dispatch, after the events read
 * with it (a release read in the same dispatch stops it first, BUG-111).
 * The desktop's system (network, sound, power, kl_system) is the
 * application's too.  A window made by kl_window_open is still a
 * connection of its own.
 *
 * Every call is made from the one thread that opened the application.
 */
struct kl_app;
struct kl_system;
struct kl_glass_panel;

/* The most descriptors an application watches. */
#define KL_APP_FDS_MAX		16U

/* What a watched descriptor is waited for, and what it became (a hang-up or an error is always told). */
#define KL_APP_FD_READ		1U
#define KL_APP_FD_WRITE	2U
#define KL_APP_FD_HANGUP	4U

/* The kinds of an application's event. */
#define KL_APP_WINDOW		1U
#define KL_APP_FD		2U
#define KL_APP_THEME		3U

/*
 * What an application is opened with.  Either pointer may be NULL:
 * display (the WAYLAND_DISPLAY one) and application (the app_id its
 * windows get when their options name none).
 */
struct kl_app_options {
	const char *display;
	const char *application;
};

/*
 * One event of an application: its kind; for KL_APP_WINDOW the window and
 * its input (as kl_window_take gave it; KL_WINDOW_ACTION for an action
 * chosen), for KL_APP_FD the descriptor and what it became (KL_APP_FD_*);
 * KL_APP_THEME (KL_VERSION 35) says the desktop's appearance changed and
 * kl_theme_default's colours with it, for the application to draw its
 * windows again (kl_appearance_get(NULL) tells which).
 */
struct kl_app_event {
	unsigned kind;
	struct kl_window *window;
	struct kl_window_event input;
	int fd;
	unsigned ready;
};

/*
 * Opens an application: connects, learns the globals and binds what every
 * window shares.  Returns NULL with errno set: a connection's error,
 * EOPNOTSUPP (no compositor or shell), EPROTO, ENOMEM.
 */
struct kl_app *kl_app_open(const struct kl_app_options *options);

/*
 * Closes the windows still open, the system, and the connection.
 */
void kl_app_close(struct kl_app *app);

/*
 * Waits up to a timeout (milliseconds, -1 for ever; no wait while events
 * are queued or a repeat is due) for the compositor or a watched
 * descriptor, and queues what happened.  Returns 0, or -1 when the
 * connection is broken.
 */
int kl_app_dispatch(struct kl_app *app, int timeout_ms);

/*
 * Watches a descriptor for KL_APP_FD_READ and KL_APP_FD_WRITE (0 stops
 * watching it): while it is ready, each dispatch queues a KL_APP_FD event.
 * Returns 0, EINVAL, or ENOSPC past KL_APP_FDS_MAX.
 */
int kl_app_watch_fd(struct kl_app *app, int fd, unsigned events);

/*
 * Takes the oldest event.  Returns 1 with it in *event, 0 when none is queued.
 */
int kl_app_take(struct kl_app *app, struct kl_app_event *event);

/*
 * The application's system, opened the first time it is asked for; NULL
 * with errno set when the compositor has none (kl_system_open).
 */
struct kl_system *kl_app_system(struct kl_app *app);

/* The application's connection, for libkeiland's other objects. */
struct wl_display *kl_app_display(const struct kl_app *app);

/*
 * Makes a window of the application (options->display is not used; a
 * NULL application takes the application's).  It is configured when this
 * returns, as kl_window_open's is.  Returns NULL with errno set as
 * kl_window_open does.
 */
struct kl_window *kl_app_window_create(struct kl_app *app, const struct kl_window_options *options);

/*
 * The declarative menus, controls and glass of a window (KL_VERSION 26).
 * The application gives each as a table, as often as it likes; the
 * library compares it with the one shown and sends only what changed.
 * An item or a control chosen is queued among the window's inputs as a
 * KL_WINDOW_ACTION input: its action in code, the item's or control's ID
 * in id, and a breadcrumb's part in begin (0 otherwise).  An action's
 * state applies to every item and control of that action, in the menu,
 * the controls and later popups.  Without the compositor's System Menu,
 * Titlebar Presentation or glass, the calls return ENOTSUP and nothing is
 * shown; the window works as before.  Each returns 0 or an errno value.
 */
#define KL_WINDOW_ACTION	17U

/* An action's state (bits; 0 is enabled, unchecked and shown). */
#define KL_ACTION_DISABLED	1U
#define KL_ACTION_CHECKED	2U
#define KL_ACTION_HIDDEN	4U

/*
 * One menu item: its ID and its parent's (KL_MENU_ROOT for a top-level
 * item), its type (KL_MENU_ITEM_*), label, action, role (KL_MENU_ROLE_*)
 * and shortcut (KL_MENU_* modifiers and an XKB keysym, 0 for none) -- the
 * fields of libkeiland's menu, in one row.
 */
struct kl_menu_entry {
	uint32_t id;
	uint32_t parent;
	unsigned type;
	const char *label;
	uint32_t action;
	unsigned role;
	unsigned modifiers;
	uint32_t keysym;
};

/*
 * One titlebar control: its ID, role (KL_CONTROL_*), priority
 * (KL_PRIORITY_*), group, label, and the action its choice queues.
 */
struct kl_control_entry {
	uint32_t id;
	unsigned role;
	unsigned priority;
	unsigned group;
	const char *label;
	uint32_t action;
};

/* The window's menu in zdesktop's System Menu (count 0 takes it away). */
int kl_window_set_menu(struct kl_window *window, const struct kl_menu_entry *entries, size_t count);

/* The window's titlebar controls (count 0 gives the titlebar back to the menu). */
int kl_window_set_controls(struct kl_window *window, const struct kl_control_entry *entries, size_t count);

/* The state of an action (KL_ACTION_* bits) in the window's menu, controls and popups. */
int kl_window_set_action_state(struct kl_window *window, uint32_t action, unsigned state);

/* A context menu of top-level items at (x, y) of the window, for its last press. */
int kl_window_popup_menu(struct kl_window *window, const struct kl_menu_entry *entries, size_t count, int x, int y);

/* The window's glass panels, from its next frame (count 0 takes them away). */
int kl_window_set_glass(struct kl_window *window, const struct kl_glass_panel *panels, size_t count);

/*
 * KL_VERSION 43 (WS131 p016): the text of a control that takes text (a
 * KL_CONTROL_SEARCH field).  As it is typed, a KL_WINDOW_CONTROL_TEXT
 * input brings it (the control's ID in id, the text in text); when its
 * editing ends, a KL_WINDOW_CONTROL_DONE input brings the text and how it
 * ended in code (KL_TEXT_SUBMITTED and the others).  A choice of a menu's
 * item or a control also stands as the window's last input for the
 * clipboard (its serial).
 */
#define KL_WINDOW_CONTROL_TEXT	19U
#define KL_WINDOW_CONTROL_DONE	20U

/* Sets a control's text and its placeholder (either may be NULL to leave it), after kl_window_set_controls. */
int kl_window_set_control_text(struct kl_window *window, uint32_t id, const char *text, const char *placeholder);

/* Gives a control's field the keyboard (the find field when Find is chosen). */
int kl_window_focus_control(struct kl_window *window, uint32_t id);

/*
 * KL_VERSION 44 (WS131 p018): the tabs of a window's titlebar, the
 * selections' changes, drag and drop, as Terminal had them of its own.
 *
 * Tabs: the titlebar shows the table given (count 0 takes them away, and
 * the titlebar shows the controls again, or the menu): each tab's ID (not
 * 0), title and KL_TAB_* flags of <keiland.h>, with KL_TABS_* options.  A
 * tab chosen, its close button or the new tab's button comes as a
 * KL_WINDOW_TAB input: KL_WINDOW_TAB_* in code, the tab's ID in id (0 for
 * a new one).
 *
 * The selections: a KL_WINDOW_SELECTION input says the clipboard or the
 * primary selection changed (KL_SELECTION_* in code), and whether it has
 * text (pressed).  kl_window_selection_own tells whether the window's own
 * text is the selection (a paste then takes it directly).
 *
 * Drops: a window takes the drags of the types it accepts
 * (KL_DROP_TEXT, KL_DROP_URIS: a "text/uri-list", which comes as it is),
 * as a copy.  A drag over it comes as KL_WINDOW_DROP_ENTER (the types it
 * has that the window takes in code, where it is in x and y) and
 * KL_WINDOW_DROP_LEAVE; dropped, as KL_WINDOW_DROP (the type it is read
 * as in code: the file names when it has them), and the window then takes
 * it with kl_window_take_drop (which finishes the drop).  A drop of the
 * window's own drag is taken directly.
 *
 * A drag of text out of the window starts with kl_window_drag_text from a
 * press (its serial); its end comes as KL_WINDOW_DRAG_DONE (code 1 when it
 * was dropped, 0 when not).
 */
#define KL_WINDOW_SELECTION	21U
#define KL_WINDOW_DROP_ENTER	22U
#define KL_WINDOW_DROP_LEAVE	23U
#define KL_WINDOW_DROP		24U
#define KL_WINDOW_DRAG_DONE	25U
#define KL_WINDOW_TAB		26U

/* Which selection changed. */
#define KL_SELECTION_CLIPBOARD	1U
#define KL_SELECTION_PRIMARY	2U

/* The types a drop is taken as (bits). */
#define KL_DROP_TEXT		1U
#define KL_DROP_URIS		2U

/* What a tab's input asks. */
#define KL_WINDOW_TAB_CHOSEN	1U
#define KL_WINDOW_TAB_CLOSE	2U
#define KL_WINDOW_TAB_NEW	3U

/* One tab: its ID (not 0), its title and its KL_TAB_* flags. */
struct kl_tab_entry {
	uint32_t id;
	const char *title;
	unsigned flags;
};

/*
 * A pen tablet: a window whose application takes it
 * (kl_window_accept_tablet) hears the pen as KL_WINDOW_TABLET_* inputs
 * with its pressure and tilt (a contact's start, moves and end, a move
 * over the window without touching it, and leaving it); any other window
 * hears the pen as the pointer.  And a held key's repeat may be turned off
 * for a window (kl_window_set_repeat).
 */
#define KL_WINDOW_TABLET_DOWN	27U
#define KL_WINDOW_TABLET_MOTION	28U
#define KL_WINDOW_TABLET_UP	29U
#define KL_WINDOW_TABLET_HOVER	30U
#define KL_WINDOW_TABLET_LEAVE	31U

/* A tablet's tool, and its barrel buttons (bits). */
#define KL_TABLET_PEN		0U
#define KL_TABLET_ERASER	1U
#define KL_TABLET_BUTTON_STYLUS	1U
#define KL_TABLET_BUTTON_STYLUS2	2U

int kl_window_accept_tablet(struct kl_window *window);

/*
 * KL_VERSION 45 (WS131 p019, Settings' own window moved here): whether the
 * window is maximized, maximizing or bringing it back and minimizing it,
 * the first screen's current mode (its refresh in millihertz; ENOENT while
 * unknown), the Vulkan device that shows the frames and the last frame's
 * times, a breadcrumb control's parts (one chosen comes as the control's
 * KL_WINDOW_ACTION with the part in begin), and the glass blurring what is
 * under the window.
 */
struct kl_present_times {
	unsigned copy_ms;
	unsigned acquire_ms;
	unsigned present_ms;
	unsigned wait_ms;
};

int kl_window_maximized(const struct kl_window *window);
void kl_window_set_maximized(struct kl_window *window, int maximized);
void kl_window_minimize(struct kl_window *window);
int kl_window_output_mode(const struct kl_window *window, int32_t *width, int32_t *height, int32_t *refresh);
const char *kl_window_device_name(const struct kl_window *window);
void kl_window_present_times(const struct kl_window *window, struct kl_present_times *times);
int kl_window_set_control_parts(struct kl_window *window, uint32_t id, const char *const *parts, size_t count);
int kl_window_set_glass_blur(struct kl_window *window, int enabled);
int kl_window_set_repeat(struct kl_window *window, int enabled);
int kl_window_set_tabs(struct kl_window *window, const struct kl_tab_entry *tabs, size_t count, unsigned options);
int kl_window_selection_own(const struct kl_window *window, unsigned which);
int kl_window_accept_drops(struct kl_window *window, unsigned types);
size_t kl_window_take_drop(struct kl_window *window, char *text, size_t size, unsigned *type);
int kl_window_drag_text(struct kl_window *window, const char *text, size_t length, uint32_t serial);

/*
 * A Vulkan surface over a window shown with KL_PRESENT_NONE, for an
 * application drawing with its own Vulkan instance (which enabled
 * VK_KHR_wayland_surface).  Declared for a program that included the
 * Vulkan header first.  Returns 0, EINVAL, or EIO when Vulkan refused.
 */
#if defined(VK_VERSION_1_0)
int kl_window_vulkan_surface(struct kl_window *window, VkInstance instance, VkSurfaceKHR *surface);
#endif

/*
 * The widgets (widgets.c, field.c, list.c, cards.c; KUI_VERSION 4,
 * plan/ws090/design.md section 3): each is drawn by one call during a
 * frame, which also records where it is for the input and reports what
 * the input did to it since the last frame (the immediate way of section
 * 2).  What a widget remembers between frames -- a field's text, a list's
 * selection and scroll -- is the application's, in a small struct it
 * keeps.  A widget draws with a style: the canvas of the frame, the text,
 * the theme, and whether the window stands on glass.
 *
 * The keyboard's focus is on one widget at a time (by id and index).  A
 * click or a tap on a widget that takes the keyboard gives it the focus;
 * Tab and Shift+Tab move it through those widgets in the order they were
 * drawn.  The keys a focused widget does not take, and every key while no
 * widget has the focus, are the application's (KL_EVENT_KEY).
 */
struct kl_style {
	struct kl_canvas *canvas;
	struct kl_text *text;
	const struct kl_theme *theme;
	int glass;
};

/* A key no widget took (an event's kind; kl_event's code and modifiers name it). */
#define KL_EVENT_KEY		9U

/* A button's look and state (bits). */
#define KL_BUTTON_PRIMARY	1U
#define KL_BUTTON_DANGER	2U
#define KL_BUTTON_DISABLED	4U

/* What a text field reports (bits). */
#define KL_FIELD_CHANGED	1U
#define KL_FIELD_SUBMITTED	2U
#define KL_FIELD_CANCELLED	4U

/* What a list reports (bits). */
#define KL_LIST_SELECTED	1U
#define KL_LIST_ACTIVATED	2U
#define KL_LIST_TOUCHED	4U	/* KUI_VERSION 5: the row was chosen by a finger's tap */

/* The longest text a field holds, with its NUL. */
#define KL_FIELD_MAX		512U

/*
 * A one-line text field's state: its UTF-8 text, the caret and the other
 * end of the selection (byte offsets on character boundaries), how far the
 * text is scrolled across, and whether its characters are shown as dots.
 */
struct kl_field {
	char text[KL_FIELD_MAX];
	size_t length;
	size_t caret;
	size_t anchor;
	int scroll;
	int secret;
};

/*
 * A list's state: how many items it has, the one selected (-1 for none),
 * and its scroll.
 */
struct kl_list {
	size_t count;
	long selected;
	struct kl_scroll scroll;
};

/* The keyboard's focus, and where the pointer is for a widget that follows it. */
int kl_ui_key(struct kl_ui *ui, uint32_t key, int pressed, unsigned modifiers);
void kl_ui_set_focus(struct kl_ui *ui, uint32_t id, uint32_t index);
void kl_ui_clear_focus(struct kl_ui *ui);
int kl_ui_has_focus(const struct kl_ui *ui, uint32_t id, uint32_t index);
void kl_ui_pointer(const struct kl_ui *ui, double *x, double *y);

/*
 * KL_VERSION 38 (BUG-203): the text an input method or the on-screen
 * keyboard sends, for the widget with the focus.  The application gives
 * each KL_WINDOW_TEXT_* input of its window to kl_ui_text: a text field
 * puts a commit in place of its selection, deletes the bytes around its
 * caret, and shows the text being composed at its caret.  After each frame
 * kl_ui_text_wanted tells whether the focused widget takes text, with its
 * caret's rectangle in the window, for kl_window_text_input and
 * kl_window_text_cursor.
 */
int kl_ui_text(struct kl_ui *ui, const struct kl_window_event *event);
int kl_ui_text_wanted(const struct kl_ui *ui, struct kl_rect *caret);

/* The widgets, each drawn and asked by one call during a frame. */
int kl_button(struct kl_ui *ui, const struct kl_style *style, uint32_t id, const struct kl_rect *rect, const char *label, unsigned flags);
int kl_button_width(const struct kl_style *style, const char *label);
int kl_switch(struct kl_ui *ui, const struct kl_style *style, uint32_t id, int x, int y, int *on, unsigned flags);
int kl_slider(struct kl_ui *ui, const struct kl_style *style, uint32_t id, const struct kl_rect *rect, double minimum, double maximum, double step, double *value);
void kl_field_set(struct kl_field *field, const char *text);
unsigned kl_field(struct kl_ui *ui, const struct kl_style *style, uint32_t id, const struct kl_rect *rect, struct kl_field *field, const char *placeholder);
int kl_list_init(struct kl_list *list);
void kl_list_release(struct kl_list *list);
unsigned kl_list_begin(struct kl_ui *ui, const struct kl_style *style, uint32_t id, const struct kl_rect *rect, struct kl_list *list, size_t count, size_t *first, size_t *last);
unsigned kl_list_row(struct kl_ui *ui, const struct kl_style *style, uint32_t id, const struct kl_rect *rect, struct kl_list *list, size_t index, struct kl_rect *row, kl_color *ink);
void kl_list_end(struct kl_ui *ui, const struct kl_style *style, const struct kl_rect *rect, struct kl_list *list);
int kl_sidebar_section(const struct kl_style *style, int x, int y, int width, const char *title);
int kl_sidebar_item(struct kl_ui *ui, const struct kl_style *style, uint32_t id, uint32_t index, const struct kl_rect *rect, enum kl_icon icon, const char *label, int current);
void kl_panel(const struct kl_style *style, const struct kl_rect *rect, int sidebar);
int kl_card(const struct kl_style *style, const struct kl_rect *rect, const char *title, const char *subtitle);
int kl_row(const struct kl_style *style, int x, int y, int width, const char *label, const char *value, int last);
int kl_header(const struct kl_style *style, int x, int y, int width, const char *title, const char *summary);
int kl_dialog(struct kl_ui *ui, const struct kl_style *style, uint32_t id, const struct kl_rect *area, const char *title, const char *body, const char *const *labels, int count);
void kl_chip(const struct kl_style *style, int centre_x, int bottom, const char *message);
void kl_progress(const struct kl_style *style, const struct kl_rect *rect, double fraction, uint64_t now_us);

/*
 * The file chooser (KUI_VERSION 5; libkeiland's keiland_file_chooser of
 * KL_VERSION 12, moved here by ws090-p006 and made of the widgets):
 * the Open and Save As window every application shares.  It shows the
 * folders and files of a folder, the sidebar's places (Recent, Home and
 * its usual folders, Computer), the filter chosen, and in Save mode takes
 * a name and asks before a file is replaced.  The answer comes once,
 * through the listener, while the application dispatches its default
 * Wayland queue; the application then destroys the chooser.  The
 * application keeps running meanwhile, and should take no input of its
 * own until the answer comes.
 *
 * The chooser is a window of its own on the application's connection, with
 * its own wl_seat objects.  Wayland sends a client's pointer, keyboard and
 * touch events to all of its objects of a seat, so an application ignores
 * the enter, key and touch events of surfaces that are not its own (as
 * kl_window does).
 */
struct kl_file_chooser;

/* What the chooser asks for: an existing file to open, or a folder and a name to save as. */
#define KL_FILE_CHOOSER_OPEN		0U
#define KL_FILE_CHOOSER_SAVE		1U

/* How it ended: a path was chosen, or the user cancelled. */
#define KL_FILE_CHOOSER_CHOSEN		0U
#define KL_FILE_CHOOSER_CANCELLED	1U

/* The most filters one chooser offers. */
#define KL_FILE_CHOOSER_FILTERS_MAX	16U

/*
 * One filter: the label it is shown by, and the file name extensions it
 * shows, separated by spaces and without their dots ("txt md c h"),
 * compared without regard to case.  NULL or empty extensions show every
 * file.  Folders are always shown.
 */
struct kl_file_filter {
	const char *label;
	const char *extensions;
};

/*
 * What a chooser starts with.  Any pointer may be NULL.
 *
 * mode: KL_FILE_CHOOSER_OPEN or _SAVE.  title: the window's title ("Open"
 * or "Save As" when NULL).  application: the app_id the window gets, so
 * that zdesktop shows it as the application's.  folder: where it starts
 * (the home folder when NULL or not a folder).  name: the name Save starts
 * with, selected up to its extension.  filters, filter_count and filter:
 * the filters offered (at most KL_FILE_CHOOSER_FILTERS_MAX) and the one
 * chosen first; without filters every file is shown.  font and
 * fallback_font: the interface's font and the one for characters it lacks
 * (the system's when NULL).
 */
struct kl_file_chooser_options {
	unsigned mode;
	const char *title;
	const char *application;
	const char *folder;
	const char *name;
	const struct kl_file_filter *filters;
	size_t filter_count;
	size_t filter;
	const char *font;
	const char *fallback_font;
};

/*
 * What a chooser tells the application, once and last: how it ended
 * (KL_FILE_CHOOSER_*), the absolute path chosen (empty when cancelled),
 * and the filter chosen last.  In Save mode the user has already agreed to
 * replace a file that exists.  The chooser's window is closed by then; the
 * application destroys the chooser, from the callback or later.
 */
struct kl_file_chooser_listener {
	void (*done)(void *data, struct kl_file_chooser *chooser, unsigned result, const char *path, size_t filter);
};

/*
 * Opens a file chooser over an application's window (parent may be NULL)
 * on the application's connection.
 *
 * Returns NULL with errno set: EINVAL (an unknown mode, too many filters,
 * a filter number past them, no listener), ENOTSUP (a compositor without
 * wl_shm or xdg_wm_base), an errno value of opening the font, ENOMEM.
 */
struct kl_file_chooser *kl_file_chooser_open(struct wl_display *display, struct xdg_toplevel *parent, const struct kl_file_chooser_options *options, const struct kl_file_chooser_listener *listener, void *data);

/*
 * Closes a chooser; one still open closes without telling.
 */
void kl_file_chooser_destroy(struct kl_file_chooser *chooser);

#ifdef __cplusplus
}
#endif

#endif
