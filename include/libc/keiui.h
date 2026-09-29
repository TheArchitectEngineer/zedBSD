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
 * memory, and which holds the clipboard and the primary selection.  Later
 * versions add the widgets.
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

#ifndef KEIUI_H
#define KEIUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes (1: the drawing -- canvas, text, icons and the theme; 2: the scroll, the input and the text view's touch; 3: the window, the clipboard and the primary selection). */
#define KUI_VERSION	3U

/*
 * Reports the interface version of the library that was loaded.
 *
 * A program built against this header may compare the result with
 * KUI_VERSION to learn whether the library it runs with is older.
 */
unsigned kui_version(void);

/* How deep the clip rectangles nest. */
#define KUI_CANVAS_CLIPS		16

/* The most corners a polygon may have. */
#define KUI_POLYGON_POINTS	96

/* How many fonts the text draws from: the main one and a fallback. */
#define KUI_TEXT_FACES		2

/* A color as 0xAARRGGBB, not premultiplied. */
typedef uint32_t kui_color;

/* An opaque color from 0xRRGGBB. */
#define KUI_RGB(value)		((kui_color)(0xff000000U | (uint32_t)(value)))

/* A color from 0xRRGGBB and an alpha from 0 to 255. */
#define KUI_RGBA(value, alpha)	((kui_color)(((uint32_t)(alpha) << 24) | ((uint32_t)(value) & 0xffffffU)))

/*
 * A rectangle of whole pixels.
 *
 * It is a plain value: a layout computes one, the drawing and the hit test
 * both read it.
 */
struct kui_rect {
	int x;
	int y;
	int width;
	int height;
};

/*
 * A picture in memory, premultiplied BGRA (0xAARRGGBB words).
 *
 * The pixels belong to whoever made the image: kui_image_create allocates
 * them and kui_image_release frees them.
 */
struct kui_image {
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
struct kui_canvas {
	/* The pixels, the words in a row and the size. */
	uint32_t *pixels;
	size_t stride;
	int width;
	int height;

	/* The clip in force, and the ones it replaced (innermost last). */
	struct kui_rect clip;
	struct kui_rect clips[KUI_CANVAS_CLIPS];
	int clip_depth;

	/* One row of polygon coverage, a float per pixel and one more. */
	float *coverage;
};

/*
 * One glyph drawn at one size, kept for the next time.
 *
 * key is zero for an empty slot; the bitmap is the glyph's coverage.
 */
struct kui_glyph {
	uint32_t key;
	int width;
	int height;
	int left;
	int top;
	int advance;
	uint8_t *bitmap;
};

/*
 * One font file: its bytes (kept for the face) and the face.
 */
struct kui_text_face {
	void *data;
	size_t size;
	struct truetype_face *face;
	unsigned pixels;
};

/*
 * The text of the window: the fonts and every glyph drawn so far.
 *
 * One lives for the whole program.  The cache is emptied when it fills up,
 * which only costs drawing the glyphs again.
 */
struct kui_text {
	struct kui_text_face faces[KUI_TEXT_FACES];
	int face_count;
	struct kui_glyph *cache;
	unsigned cache_size;
	unsigned cache_used;
	uint8_t *scratch;
	size_t scratch_size;
};

/*
 * The vertical measurements of text at one size, in pixels.
 */
struct kui_text_line {
	int ascent;
	int descent;
	int height;
};

/*
 * The icons drawn with lines (sidebar, toolbar) or filled shapes (items).
 */
enum kui_icon {
	KUI_ICON_HOME,
	KUI_ICON_DESKTOP,
	KUI_ICON_DOCUMENTS,
	KUI_ICON_DOWNLOADS,
	KUI_ICON_PICTURES,
	KUI_ICON_MUSIC,
	KUI_ICON_MOVIES,
	KUI_ICON_FOLDER_LINE,
	KUI_ICON_RECENTS,
	KUI_ICON_TRASH,
	KUI_ICON_COMPUTER,
	KUI_ICON_VOLUME,
	KUI_ICON_BACK,
	KUI_ICON_FORWARD,
	KUI_ICON_SEARCH,
	KUI_ICON_GRID,
	KUI_ICON_LIST,
	KUI_ICON_PREVIEW,
	KUI_ICON_CHEVRON,
	KUI_ICON_CLOSE,
	KUI_ICON_PLUS,
	KUI_ICON_UP,
	KUI_ICON_DOWN,

	/* The line pictures (Settings' pages), from KUI_ICON_TILES on, in the order Settings numbered them. */
	KUI_ICON_TILES,
	KUI_ICON_WIFI,
	KUI_ICON_ETHERNET,
	KUI_ICON_BLUETOOTH,
	KUI_ICON_SHIELD,
	KUI_ICON_GLOBE,
	KUI_ICON_PALETTE,
	KUI_ICON_PICTURE,
	KUI_ICON_BELL,
	KUI_ICON_SPEAKER,
	KUI_ICON_MONITOR,
	KUI_ICON_DISK,
	KUI_ICON_BATTERY,
	KUI_ICON_KEYBOARD,
	KUI_ICON_MOUSE,
	KUI_ICON_TOUCHPAD,
	KUI_ICON_PRINTER,
	KUI_ICON_SHARE,
	KUI_ICON_PEOPLE,
	KUI_ICON_EYE,
	KUI_ICON_LOCK,
	KUI_ICON_PERSON,
	KUI_ICON_REFRESH,
	KUI_ICON_INFO,
	KUI_ICON_DISCLOSURE
};

/* The canvas (canvas.c). */
int kui_canvas_init(struct kui_canvas *canvas, uint32_t *pixels, size_t stride, int width, int height);
void kui_canvas_release(struct kui_canvas *canvas);
void kui_canvas_clip_push(struct kui_canvas *canvas, const struct kui_rect *rect);
void kui_canvas_clip_pop(struct kui_canvas *canvas);
void kui_canvas_clear(struct kui_canvas *canvas);
void kui_canvas_fill(struct kui_canvas *canvas, const struct kui_rect *rect, kui_color color);
void kui_canvas_gradient(struct kui_canvas *canvas, const struct kui_rect *rect, kui_color top, kui_color bottom);
void kui_canvas_round(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, kui_color color);
void kui_canvas_round_gradient(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, kui_color top, kui_color bottom);
void kui_canvas_round_border(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, float thickness, kui_color color);
void kui_canvas_shadow(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, float softness, kui_color color);
void kui_canvas_circle(struct kui_canvas *canvas, float cx, float cy, float radius, kui_color color);
void kui_canvas_ring(struct kui_canvas *canvas, float cx, float cy, float radius, float thickness, float fraction, kui_color color);
void kui_canvas_polygon(struct kui_canvas *canvas, const float *points, int count, kui_color color);
void kui_canvas_line(struct kui_canvas *canvas, float x0, float y0, float x1, float y1, float thickness, kui_color color);
void kui_canvas_mask(struct kui_canvas *canvas, int x, int y, const uint8_t *mask, int width, int height, size_t stride, kui_color color);
void kui_canvas_image(struct kui_canvas *canvas, const struct kui_image *image, float x, float y, float width, float height, float radius, float opacity);
int kui_image_create(struct kui_image *image, int width, int height);
void kui_image_release(struct kui_image *image);
void kui_image_scale(const struct kui_image *source, struct kui_image *target);
kui_color kui_color_mix(kui_color from, kui_color to, float amount);

/* The text (text.c). */
int kui_text_open(struct kui_text *text, const char *primary, const char *fallback);
void kui_text_close(struct kui_text *text);
void kui_text_metrics(struct kui_text *text, unsigned pixels, struct kui_text_line *line);
int kui_text_center(unsigned pixels, int top, int height);
int kui_text_width(struct kui_text *text, const char *string, size_t length, unsigned pixels, int bold);
int kui_text_draw(struct kui_text *text, struct kui_canvas *canvas, int x, int baseline, const char *string, size_t length, unsigned pixels, int bold, kui_color color);
int kui_text_draw_fit(struct kui_text *text, struct kui_canvas *canvas, int x, int baseline, const char *string, unsigned pixels, int bold, int width, kui_color color);
size_t kui_text_fit(struct kui_text *text, const char *string, unsigned pixels, int bold, int width, char *out, size_t size);
size_t kui_text_break(struct kui_text *text, const char *string, unsigned pixels, int bold, int width);
uint32_t kui_utf8_next(const char *string, size_t length, size_t *index);

/*
 * The theme: the colours and sizes of the Kei look, which every widget
 * draws with (the file manager's values, plan/ws071/spec.md).  One theme
 * exists so far, the light one; an application reads it and does not
 * change it.
 */
struct kui_theme {
	/* The window's ground (a vertical gradient) and the cards on it. */
	kui_color ground_top;
	kui_color ground_bottom;
	kui_color panel;
	kui_color panel_edge;
	kui_color shadow;

	/* A sidebar and a content card standing on glass, and on the plain ground. */
	kui_color glass_sidebar;
	kui_color glass_content;
	kui_color sidebar;

	/* Text: the main ink, the secondary and the faint, and an icon's ink. */
	kui_color text;
	kui_color text_secondary;
	kui_color text_faint;
	kui_color icon;

	/* The accent, a selection with and without the keyboard, the pointer's hover, a separator, a folder and danger. */
	kui_color accent;
	kui_color selection;
	kui_color selection_inactive;
	kui_color hover;
	kui_color separator;
	kui_color folder;
	kui_color danger;

	/* A card's and a control's corner radius, a list row's height, and the text sizes of body, secondary and title text. */
	float card_radius;
	float control_radius;
	int row_height;
	unsigned text_body;
	unsigned text_small;
	unsigned text_title;
};

/* The theme (theme.c). */
const struct kui_theme *kui_theme_default(void);

/* The icons (icons.c and icons-line.c). */
void kui_icon_draw(struct kui_canvas *canvas, enum kui_icon icon, float x, float y, float size, kui_color color);
void kui_icon_folder(struct kui_canvas *canvas, float x, float y, float size, kui_color tint);
void kui_icon_file(struct kui_canvas *canvas, struct kui_text *text, float x, float y, float size, kui_color band, const char *label);
void kui_icon_tag(struct kui_canvas *canvas, float cx, float cy, float radius, kui_color color);

/*
 * The scroll (scroll.c, KUI_VERSION 2): the state of one part of a window
 * whose content is larger than the part, which an application keeps and
 * draws its content at (x, y) of.
 *
 * The wheel and the keys glide the content to where they send it (the
 * distance left shrinks by e every KUI_SCROLL_GLIDE_US); a finger drags it
 * and lets it fly on with libkeiland's scroller (the inertia and the
 * rubber band past an end are the scroller's).  Outside a finger's hold
 * the position stays within 0..content-viewport on each axis that
 * scrolls.  The scroll bars show while the content moves and fade out
 * over KUI_SCROLL_FADE_US after it stops.
 *
 * The fields are read by the application (x and y above all); they are
 * written only through the calls.  Nothing here draws but
 * kui_scroll_draw_bars, so a program that draws its own content with
 * Vulkan (Terminal, Notes) uses the same scroll without the canvas.
 */
struct keiland_scroller;

/* The axes a scroll moves along. */
#define KUI_SCROLL_X		1U
#define KUI_SCROLL_Y		2U

/* How quickly a glide closes on its target (the time constant), and how long the bars take to fade. */
#define KUI_SCROLL_GLIDE_US	70000U
#define KUI_SCROLL_FADE_US	1000000U

struct kui_scroll {
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
	struct keiland_scroller *scroller;
	int touched;
	int released;

	/* When the content last moved (for the bars), 0 before it ever moved. */
	uint64_t moved_us;
};

int kui_scroll_init(struct kui_scroll *scroll, unsigned axes);
void kui_scroll_release(struct kui_scroll *scroll);
void kui_scroll_set_size(struct kui_scroll *scroll, double content_width, double content_height, double viewport_width, double viewport_height);
void kui_scroll_wheel(struct kui_scroll *scroll, double dx, double dy, uint64_t now_us);
void kui_scroll_move_to(struct kui_scroll *scroll, double x, double y, int glide, uint64_t now_us);
void kui_scroll_reveal(struct kui_scroll *scroll, const struct kui_rect *rect, uint64_t now_us);
int kui_scroll_key(struct kui_scroll *scroll, uint32_t key, unsigned modifiers, double line, uint64_t now_us);
int kui_scroll_press(struct kui_scroll *scroll, uint64_t now_us);
void kui_scroll_drag(struct kui_scroll *scroll, double dx, double dy);
void kui_scroll_fling(struct kui_scroll *scroll, double vx, double vy, uint64_t now_us);
void kui_scroll_cancel(struct kui_scroll *scroll, uint64_t now_us);
int kui_scroll_step(struct kui_scroll *scroll, uint64_t now_us);
double kui_scroll_limit_x(const struct kui_scroll *scroll);
double kui_scroll_limit_y(const struct kui_scroll *scroll);
int kui_scroll_draw_bars(const struct kui_scroll *scroll, struct kui_canvas *canvas, const struct kui_rect *viewport, const struct kui_theme *theme, uint64_t now_us);

/*
 * The keys (input.c, KUI_VERSION 2).  zdesktop forwards evdev key codes
 * with no keymap; the library carries the US layout, as the desktop's
 * programs do, until an input method arrives (WS095).
 */
#define KUI_KEY_ESC		1U
#define KUI_KEY_BACKSPACE	14U
#define KUI_KEY_TAB		15U
#define KUI_KEY_ENTER		28U
#define KUI_KEY_SPACE		57U
#define KUI_KEY_KPENTER	96U
#define KUI_KEY_HOME		102U
#define KUI_KEY_UP		103U
#define KUI_KEY_PAGEUP		104U
#define KUI_KEY_LEFT		105U
#define KUI_KEY_RIGHT		106U
#define KUI_KEY_END		107U
#define KUI_KEY_DOWN		108U
#define KUI_KEY_PAGEDOWN	109U
#define KUI_KEY_DELETE		111U

/* The modifiers held. */
#define KUI_MOD_SHIFT		1U
#define KUI_MOD_CTRL		2U
#define KUI_MOD_ALT		4U
#define KUI_MOD_SUPER		8U

uint32_t kui_key_character(uint32_t key, unsigned modifiers);

/*
 * The touch of a view of editable text (text-touch.c, KUI_VERSION 2,
 * plan/ws090/design.md section 6.1): in a text editor's body and a text
 * field, one finger's drag selects and two fingers scroll.
 *
 * A tap puts the caret, a double tap selects a word, one finger's drag
 * selects from where it touched (the content scrolls by itself while the
 * finger is near the view's edge), and a long press asks for the context
 * menu.  A selection made by touch shows a handle at each end; dragging a
 * handle moves that end.  Two fingers are the scroll's (kui_ui gives them
 * to it).
 *
 * The view gives three answers in its content's coordinates (the scroll
 * already undone): the text position nearest a point, the caret's
 * rectangle at a position, and the word around a position.  Positions are
 * whatever the view counts in (byte offsets in Text Editor).
 */
struct kui_text_view {
	size_t (*position_at)(void *data, double x, double y);
	void (*caret_rect)(void *data, size_t position, struct kui_rect *rect);
	void (*word_at)(void *data, size_t position, size_t *start, size_t *end);
};

/* What the fingers changed, for kui_text_touch_take. */
#define KUI_TEXT_TOUCH_SELECTION	1U
#define KUI_TEXT_TOUCH_MENU		2U

/* The handles: their drawn diameter and the diameter a finger finds them within. */
#define KUI_TEXT_HANDLE		12
#define KUI_TEXT_HANDLE_REACH	44

/* How near the view's edge a selecting finger scrolls the content, and how fast at the edge (pixels a second). */
#define KUI_TEXT_EDGE		24
#define KUI_TEXT_EDGE_SPEED	1200.0

/* Which end of the selection a handle drag moves. */
#define KUI_TEXT_HANDLE_NONE	0
#define KUI_TEXT_HANDLE_ANCHOR	1
#define KUI_TEXT_HANDLE_CARET	2

struct kui_text_touch {
	/* The view's answers and their data. */
	const struct kui_text_view *view;
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

	/* What changed since kui_text_touch_take, and where the context menu was asked for (window coordinates). */
	unsigned changes;
	double menu_x;
	double menu_y;
};

void kui_text_touch_init(struct kui_text_touch *touch, const struct kui_text_view *view, void *data);
void kui_text_touch_set_selection(struct kui_text_touch *touch, size_t anchor, size_t caret);
void kui_text_touch_tap(struct kui_text_touch *touch, double x, double y, int twice);
void kui_text_touch_long_press(struct kui_text_touch *touch, double window_x, double window_y);
void kui_text_touch_drag_begin(struct kui_text_touch *touch, double x, double y);
void kui_text_touch_drag(struct kui_text_touch *touch, double x, double y);
void kui_text_touch_drag_end(struct kui_text_touch *touch);
int kui_text_touch_edge(const struct kui_text_touch *touch, const struct kui_scroll *scroll, double *vx, double *vy);
unsigned kui_text_touch_take(struct kui_text_touch *touch);
void kui_text_touch_draw_handles(const struct kui_text_touch *touch, struct kui_canvas *canvas, double origin_x, double origin_y, const struct kui_theme *theme);

/*
 * The input of a window (ui.c, KUI_VERSION 2, design section 4): which
 * part of a frame a pointer, the wheel or a finger meant.
 *
 * While a frame is drawn, each part that takes input is recorded: a
 * widget (kui_ui_hit, by an id the application chooses and an index), a
 * scroll's viewport (kui_ui_scroll_region) and a view of editable text
 * (kui_ui_text_region).  Input that arrives before the next frame is
 * resolved against the parts of the frame last drawn, the latest recorded
 * first (on top): a press and a release on the same widget click it (twice
 * within 400 ms: a double click), the wheel goes to the scroll under the
 * pointer, and a finger goes by libkeiland's gestures: a tap to the widget
 * it touched (else to the text view there), a drag to the scroll or text
 * view it touched (a widget such as a list's row inside a scroll does not
 * take a drag; the scroll does).  In a text view one finger's drag selects
 * and two fingers' drag scrolls (kui_text_touch).  Input that meets no
 * part is kept for the application (kui_ui_take; a drag's distance from
 * kui_ui_drag_offset).  Each input call returns 1 when the window must
 * draw again.
 *
 * A scroll or a text touch given to kui_ui_scroll_region or
 * kui_ui_text_region must live until the next frame is drawn (the input
 * in between reaches it).
 */
struct kui_ui;

/* What a widget's record reports of the input (bits). */
#define KUI_HIT_HOT		1U	/* the pointer is over it */
#define KUI_HIT_ACTIVE		2U	/* a press on it is held */
#define KUI_HIT_CLICKED	4U	/* pressed and released on it since the last frame */
#define KUI_HIT_DOUBLE		8U	/* the click was the second of a double click or tap */

/* The input no part took. */
#define KUI_EVENT_PRESS		1U
#define KUI_EVENT_RELEASE	2U
#define KUI_EVENT_WHEEL		3U
#define KUI_EVENT_TAP		4U
#define KUI_EVENT_DOUBLE_TAP	5U
#define KUI_EVENT_LONG_PRESS	6U
#define KUI_EVENT_DRAG_BEGIN	7U
#define KUI_EVENT_DRAG_END	8U

/*
 * One input no part took: its kind, where (window coordinates), the
 * wheel's distance or a drag's velocity, the fingers down, and the id of
 * the region it happened over (a long press over a text view: that view's
 * id, 0 over none).
 */
struct kui_event {
	unsigned kind;
	double x;
	double y;
	double dx;
	double dy;
	unsigned fingers;
	uint32_t region;
};

struct kui_ui *kui_ui_create(void);
void kui_ui_destroy(struct kui_ui *ui);
int kui_ui_pointer_motion(struct kui_ui *ui, double x, double y);
int kui_ui_pointer_leave(struct kui_ui *ui);
int kui_ui_pointer_button(struct kui_ui *ui, int pressed, uint64_t now_us);
int kui_ui_wheel(struct kui_ui *ui, double dx, double dy, uint64_t now_us);
int kui_ui_touch_down(struct kui_ui *ui, int32_t id, uint64_t time_us, uint64_t now_us, double x, double y);
int kui_ui_touch_motion(struct kui_ui *ui, int32_t id, uint64_t time_us, uint64_t now_us, double x, double y);
int kui_ui_touch_up(struct kui_ui *ui, int32_t id, uint64_t time_us, uint64_t now_us);
int kui_ui_touch_cancel(struct kui_ui *ui, uint64_t now_us);
void kui_ui_begin(struct kui_ui *ui, uint64_t now_us);
unsigned kui_ui_hit(struct kui_ui *ui, uint32_t id, uint32_t index, const struct kui_rect *rect);
void kui_ui_scroll_region(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, struct kui_scroll *scroll);
void kui_ui_text_region(struct kui_ui *ui, uint32_t id, const struct kui_rect *rect, struct kui_scroll *scroll, struct kui_text_touch *touch);
int kui_ui_end(struct kui_ui *ui, uint64_t now_us);
int kui_ui_take(struct kui_ui *ui, struct kui_event *event);
int kui_ui_drag_offset(struct kui_ui *ui, uint64_t now_us, double *dx, double *dy);

/*
 * The window (window.c, present.c, present-shm.c, clipboard.c,
 * primary.c; KUI_VERSION 3, plan/ws090/design.md section 5): an
 * xdg-shell toplevel of its own connection, its seat's input, the frames
 * the application draws on the CPU, and the clipboard and the primary
 * selection.
 *
 * The input arrives as events in a queue the application takes after
 * each kui_window_dispatch, in the order they came: the pointer, the
 * wheel, the keys (a held key repeats: the application calls
 * kui_window_repeat after the dispatch, so that a release read in the
 * same dispatch stops it first, BUG-111), the keyboard's focus, the
 * fingers (their times turned into CLOCK_MONOTONIC microseconds), a new
 * size and the request to close.  An application posts its own inputs
 * heard through other objects during a dispatch (a System Menu's shortcut,
 * a titlebar's control) with kui_window_post, so that they keep their
 * place among the keys (typed text, then Ctrl+S).  Input on the program's other surfaces
 * (a file chooser's window) is not the window's and never queued.
 *
 * A frame is ordinary memory of premultiplied 0xAARRGGBB words the size
 * kui_window_present_resize reported.  KUI_PRESENT_VULKAN shows it through
 * a Vulkan swapchain (see-through when the compositor offers it, the way
 * zdesktop's glass needs), KUI_PRESENT_SHM through wl_shm buffers (for a
 * small window of a library, or where Vulkan is missing), and
 * KUI_PRESENT_NONE leaves the surface to the application's own Vulkan.
 * The menus, the titlebar's controls and the glass panels stay the
 * application's (libkeiland), on the objects the accessors give.
 */
struct kui_window;
struct wl_display;
struct wl_surface;
struct wl_seat;
struct xdg_toplevel;

/* How the frames are shown. */
#define KUI_PRESENT_VULKAN	0U
#define KUI_PRESENT_SHM		1U
#define KUI_PRESENT_NONE	2U

/* The kinds of input. */
#define KUI_WINDOW_MOTION	1U
#define KUI_WINDOW_LEAVE	2U
#define KUI_WINDOW_BUTTON	3U
#define KUI_WINDOW_AXIS		4U
#define KUI_WINDOW_KEY		5U
#define KUI_WINDOW_FOCUS	6U
#define KUI_WINDOW_TOUCH_DOWN	7U
#define KUI_WINDOW_TOUCH_MOTION	8U
#define KUI_WINDOW_TOUCH_UP	9U
#define KUI_WINDOW_TOUCH_CANCEL	10U
#define KUI_WINDOW_RESIZE	11U
#define KUI_WINDOW_CLOSE	12U
#define KUI_WINDOW_POST		13U

/* The evdev codes of the pointer's buttons. */
#define KUI_BUTTON_LEFT		0x110U
#define KUI_BUTTON_RIGHT	0x111U
#define KUI_BUTTON_MIDDLE	0x112U

/*
 * What a window is made with.  Any pointer may be NULL: display (the
 * WAYLAND_DISPLAY one), title and application (the app_id).  width and
 * height are the size asked for until the compositor gives one.
 */
struct kui_window_options {
	const char *display;
	const char *title;
	const char *application;
	uint32_t width;
	uint32_t height;
	unsigned present;
};

/*
 * One input: its kind (KUI_WINDOW_*), where the pointer or the finger is
 * (surface pixels), a button's or a key's code and whether it is pressed
 * (a focus: 1 when it came), whether a key is a repeat, the modifiers held
 * (KUI_MOD_*), the wheel's distance in pixels, a finger's id and the time
 * it happened, when it was read, and its serial (a press's, for a popup or
 * a selection).
 */
struct kui_window_event {
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
};

struct kui_window *kui_window_open(const struct kui_window_options *options);
void kui_window_close(struct kui_window *window);
int kui_window_dispatch(struct kui_window *window, int timeout_ms);
int kui_window_take(struct kui_window *window, struct kui_window_event *event);
void kui_window_post(struct kui_window *window, uint32_t code);
int kui_window_repeat(struct kui_window *window, uint64_t now_us);
int kui_window_repeat_wait(const struct kui_window *window, uint64_t now_us);
void kui_window_set_title(struct kui_window *window, const char *title);
void kui_window_size(const struct kui_window *window, uint32_t *width, uint32_t *height);
int kui_window_present_resize(struct kui_window *window, uint32_t *width, uint32_t *height);
int kui_window_present(struct kui_window *window, const uint32_t *pixels, size_t stride);
int kui_window_see_through(const struct kui_window *window);
struct wl_display *kui_window_display(const struct kui_window *window);
struct wl_surface *kui_window_surface(const struct kui_window *window);
struct xdg_toplevel *kui_window_toplevel(const struct kui_window *window);
uint32_t kui_window_serial(const struct kui_window *window);
uint32_t kui_window_press_serial(const struct kui_window *window);
void kui_window_set_serial(struct kui_window *window, uint32_t serial);
struct wl_seat *kui_window_seat(const struct kui_window *window);
void kui_window_copy(struct kui_window *window, const char *text, size_t length);
size_t kui_window_paste(struct kui_window *window, char *text, size_t size);
int kui_window_can_paste(const struct kui_window *window);
void kui_window_select(struct kui_window *window, const char *text, size_t length);
size_t kui_window_paste_primary(struct kui_window *window, char *text, size_t size);
uint64_t kui_clock_us(void);

#ifdef __cplusplus
}
#endif

#endif
