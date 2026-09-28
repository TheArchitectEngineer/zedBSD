/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * PDF Viewer (ws079-p006): the parts that know neither Wayland nor Vulkan.
 *
 * The viewer draws each frame on the CPU into a canvas of premultiplied
 * 0xAARRGGBB words, which the presenter (present.c) shows with Vulkan.
 * Pages are interpreted by libpdf into display lists and rasterized by
 * libpdf's CPU rasterizer at the zoom in force; the rasters are kept in a
 * cache and copied into the frame.  The view (view.c) lays the pages out
 * in one of two modes: a vertical continuous scroll, or one page at a time
 * turned by a sideways drag (a swipe) or the keys.
 */

#ifndef PDFVIEWER_VIEWER_H
#define PDFVIEWER_VIEWER_H

#include <stddef.h>
#include <stdint.h>

#include <pdf.h>

struct truetype_face;

/* The window's size when it opens. */
#define PV_WIDTH		1000U
#define PV_HEIGHT		760U

/* The longest path the viewer keeps. */
#define PV_PATH_MAX		1024

/* The modifiers of an input. */
#define PV_MOD_SHIFT		0x01U
#define PV_MOD_CTRL		0x02U
#define PV_MOD_ALT		0x04U
#define PV_MOD_SUPER		0x08U

/* The evdev codes of the keys the viewer answers. */
#define PV_KEY_ESCAPE		1U
#define PV_KEY_0		11U
#define PV_KEY_MINUS		12U
#define PV_KEY_EQUAL		13U
#define PV_KEY_BACKSPACE	14U
#define PV_KEY_Q		16U
#define PV_KEY_W		17U
#define PV_KEY_E		18U
#define PV_KEY_O		24U
#define PV_KEY_ENTER		28U
#define PV_KEY_SPACE		57U
#define PV_KEY_KP_MINUS		74U
#define PV_KEY_KP_PLUS		78U
#define PV_KEY_HOME		102U
#define PV_KEY_UP		103U
#define PV_KEY_PAGE_UP		104U
#define PV_KEY_LEFT		105U
#define PV_KEY_RIGHT		106U
#define PV_KEY_END		107U
#define PV_KEY_DOWN		108U
#define PV_KEY_PAGE_DOWN	109U

/* The pointer's left button (evdev BTN_LEFT). */
#define PV_BUTTON_LEFT		0x110U

/*
 * The kinds of input the window queues for the viewer.
 */
enum pv_event_type {
	PV_EVENT_MOTION = 0,
	PV_EVENT_BUTTON,
	PV_EVENT_AXIS,
	PV_EVENT_LEAVE,
	PV_EVENT_KEY,
	PV_EVENT_ACTION
};

/*
 * One input: where the pointer was, what happened, the modifiers held and
 * when (milliseconds of the monotonic clock).  Only the fields of its type
 * are meaningful.
 */
struct pv_event {
	enum pv_event_type type;
	int x;
	int y;
	uint32_t button;
	int pressed;
	int scroll;
	uint32_t key;
	uint32_t modifiers;
	uint64_t time;
	uint32_t action;
};

/*
 * The things the viewer can be asked to do, by its menus, its titlebar or
 * its keys.
 */
enum pv_action {
	PV_ACTION_NONE = 0,
	PV_ACTION_OPEN,
	PV_ACTION_CLOSE,
	PV_ACTION_QUIT,
	PV_ACTION_ANNOTATE,
	PV_ACTION_MODE_SCROLL,
	PV_ACTION_MODE_PAGE,
	PV_ACTION_FIT_WIDTH,
	PV_ACTION_FIT_PAGE,
	PV_ACTION_ZOOM_IN,
	PV_ACTION_ZOOM_OUT,
	PV_ACTION_ZOOM_RESET,
	PV_ACTION_PREVIOUS,
	PV_ACTION_NEXT,
	PV_ACTION_FIRST,
	PV_ACTION_LAST
};

/*
 * How the pages are laid out.
 */
enum pv_mode {
	PV_MODE_SCROLL = 0,
	PV_MODE_PAGE
};

/*
 * How the zoom is chosen: to fit the width, to fit the page, or a scale
 * the user set.
 */
enum pv_fit {
	PV_FIT_WIDTH = 0,
	PV_FIT_PAGE,
	PV_FIT_CUSTOM
};

/*
 * A surface to draw on: the caller's pixels (premultiplied 0xAARRGGBB),
 * the words in a row, and the size.
 */
struct pv_canvas {
	uint32_t *pixels;
	size_t stride;
	int width;
	int height;
};

/*
 * One font file: its bytes (kept for the face), the face, and the size the
 * face is set to.
 */
struct pv_text {
	void *data;
	size_t size;
	struct truetype_face *face;
	unsigned pixels;
	unsigned char *scratch;
	size_t scratch_size;
};

/*
 * One page of the open document: its shown size in points, its display
 * list once interpreted (NULL before), and its raster at one scale once
 * drawn.  used orders the rasters for the cache's eviction.
 */
struct pv_page {
	double width;
	double height;
	struct pdf_display_list *list;
	int list_error;
	uint32_t *raster;
	int raster_width;
	int raster_height;
	double raster_scale;
	uint64_t used;
};

/*
 * The open document: its path, libpdf's document, its pages, the bytes the
 * page rasters take, and the flags of the pages drawn so far.
 */
struct pv_document {
	char path[PV_PATH_MAX];
	struct pdf_document *document;
	struct pv_page *pages;
	size_t count;
	double widest;
	double tallest;
	size_t raster_bytes;
	uint64_t clock;
	unsigned flags;
};

/* The most entries the file chooser lists. */
#define PV_CHOOSER_ENTRIES	512

/*
 * One entry of the file chooser: a folder or a PDF file of the folder
 * shown.
 */
struct pv_entry {
	char name[256];
	int folder;
};

/*
 * The file chooser: the folder shown, its folders and PDF files (folders
 * first, each group by name), the entry selected, and the first entry in
 * view.
 */
struct pv_chooser {
	char folder[PV_PATH_MAX];
	struct pv_entry *entries;
	size_t count;
	size_t selected;
	size_t first;
};

/*
 * The viewer: the document, how it is laid out and where the view is, the
 * pointer's drag or swipe, the page turn in progress, the message shown,
 * the file chooser, and what the window is asked to do.
 *
 * scroll_y is the top of the view in the laid-out document, in pixels (the
 * scroll mode's whole column of pages, or the page mode's one page);
 * scroll_x is the left of the view when the pages are wider than it.
 * swipe is how far the page of the page mode is dragged sideways, and a
 * turn moves it from turn_from to turn_to between turn_start and
 * turn_start + PV_TURN_MS.  dirty says the frame must be drawn again.
 */
struct pv_app {
	struct pv_document document;
	int has_document;
	enum pv_mode mode;
	enum pv_fit fit;
	double zoom;
	int width;
	int height;
	double scroll_x;
	double scroll_y;
	size_t page;
	int pressed;
	int dragging;
	int press_x;
	int press_y;
	int last_x;
	int last_y;
	uint64_t last_time;
	double velocity_x;
	double press_scroll_x;
	double press_scroll_y;
	double swipe;
	int turning;
	double turn_from;
	double turn_to;
	int turn_direction;
	uint64_t turn_start;
	double wheel;
	char message[256];
	uint64_t message_until;
	uint64_t indicator_until;
	int choosing;
	struct pv_chooser chooser;
	int want_close;
	int want_annotate;
	int opened;
	uint64_t now;
	int dirty;
	struct pv_text *text;
};

/* The document and its page cache (document.c). */
int pv_document_open(struct pv_document *document, const char *path);
void pv_document_close(struct pv_document *document);
int pv_document_raster(struct pv_document *document, size_t index, double scale, const struct pv_page **page);
void pv_document_trim(struct pv_document *document, size_t keep_first, size_t keep_last);

/* The view: layout, navigation and input (view.c). */
void pv_app_init(struct pv_app *app, struct pv_text *text, int width, int height);
void pv_app_release(struct pv_app *app);
int pv_app_open(struct pv_app *app, const char *path);
void pv_app_close_document(struct pv_app *app);
void pv_app_resize(struct pv_app *app, int width, int height);
void pv_app_event(struct pv_app *app, const struct pv_event *event);
void pv_app_action(struct pv_app *app, enum pv_action action);
int pv_app_tick(struct pv_app *app, uint64_t now);
int pv_app_prefetch(struct pv_app *app);
size_t pv_app_current_page(const struct pv_app *app);
double pv_app_scale(const struct pv_app *app, size_t index);
double pv_app_neighbour_distance(const struct pv_app *app, size_t neighbour);
double pv_app_page_top(const struct pv_app *app, size_t index);
double pv_app_content_height(const struct pv_app *app);
double pv_app_content_width(const struct pv_app *app);
void pv_app_message(struct pv_app *app, const char *message, uint64_t duration);
void pv_chooser_layout(const struct pv_app *app, int *x, int *y, int *width, int *height, size_t *rows);

/* The sizes of the layout, which the view and the frame share. */
#define PV_MARGIN		16
#define PV_GAP			16
#define PV_CHOOSER_HEADER	52
#define PV_CHOOSER_ROW		34

/* The frame (draw.c). */
void pv_draw(struct pv_app *app, struct pv_canvas *canvas);

/* The file chooser (chooser.c). */
int pv_chooser_open(struct pv_chooser *chooser, const char *folder);
void pv_chooser_close(struct pv_chooser *chooser);
int pv_chooser_path(const struct pv_chooser *chooser, size_t index, char *path, size_t size);

/* The canvas (canvas.c). */
void pv_canvas_fill(struct pv_canvas *canvas, int x, int y, int width, int height, uint32_t color);
void pv_canvas_blend(struct pv_canvas *canvas, int x, int y, int width, int height, uint32_t color);
void pv_canvas_round(struct pv_canvas *canvas, int x, int y, int width, int height, int radius, uint32_t color);
void pv_canvas_copy(struct pv_canvas *canvas, int x, int y, const uint32_t *pixels, int width, int height);
void pv_canvas_mask(struct pv_canvas *canvas, int x, int y, const unsigned char *mask, int width, int height, uint32_t color);

/* The text (text.c). */
int pv_text_open(struct pv_text *text, const char *path);
void pv_text_close(struct pv_text *text);
int pv_text_width(struct pv_text *text, const char *string, unsigned pixels);
void pv_text_draw(struct pv_text *text, struct pv_canvas *canvas, int x, int baseline, const char *string, unsigned pixels, uint32_t color);

/* The log (view.c). */
void pv_log(const char *format, ...);
uint64_t pv_clock(void);

#endif
