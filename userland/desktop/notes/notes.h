/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of Notes, the handwritten notebook (plan/ws079/design-input-notes.md
 * section 5, plan/ws079/design-pdf.md).
 *
 * document.c keeps the pages and their strokes, and the undo history;
 * encode.c turns a document into the edit data (ZNOT) the saved PDF carries
 * and back; journal.c keeps an append-only log of every change since the
 * last save, so that a crash loses nothing; save.c writes the PDF with
 * libpdf; geometry.c turns strokes into the triangles the renderer draws;
 * render.c draws them with Vulkan; ui.c draws the toolbar; window.c holds
 * the Wayland window and turns the seat's input into Notes' own input
 * events; menu.c gives the compositor the window's menus; main.c ties them
 * together.
 *
 * The model, the edit data, the journal and the PDF writer depend on the C
 * library and libpdf only (the host tests build them), and the window
 * system is confined to window.c, render.c and menu.c.
 */

#ifndef NOTES_H
#define NOTES_H

#include <stddef.h>
#include <stdint.h>

#include <pdf.h>

/* The page's default size, A4 portrait in points (design-input-notes.md D4). */
#define NOTES_PAGE_WIDTH	595.276f
#define NOTES_PAGE_HEIGHT	841.890f

/* The kinds of tool a stroke is drawn with (the edit data's tool type). */
#define NOTES_TOOL_PEN		0U
#define NOTES_TOOL_HIGHLIGHTER	1U

/* The page backgrounds (the edit data's background kind); v1 draws plain pages. */
#define NOTES_BACKGROUND_PLAIN	0U

/* The pressure of one sample runs from 0 to this value. */
#define NOTES_PRESSURE_MAX	65535U

/* The unit the edit data stores lengths in: 1/64 point. */
#define NOTES_UNITS_PER_POINT	64.0f

/* How many changes the undo history keeps. */
#define NOTES_UNDO_LIMIT	1000U

/* How long after the last change the document is saved on its own, in milliseconds. */
#define NOTES_AUTOSAVE_IDLE_MS	5000U

/* The name and media type of the edit data attached to the PDF (design-pdf.md section 2). */
#define NOTES_ATTACHMENT_NAME	"kei-notes.bin"
#define NOTES_ATTACHMENT_TYPE	"application/x-kei-notes"

/* The sources of an input event: a mouse (or a pen without the tablet protocol), a pen's tip, its eraser end. */
#define NOTES_SOURCE_POINTER	0U
#define NOTES_SOURCE_PEN	1U
#define NOTES_SOURCE_ERASER	2U

/*
 * The kinds of input event: contact starts, the point moves in contact,
 * contact ends; a pen moves over the window without touching it, a pen
 * leaves the window.
 */
#define NOTES_INPUT_DOWN	1U
#define NOTES_INPUT_MOTION	2U
#define NOTES_INPUT_UP		3U
#define NOTES_INPUT_HOVER	4U
#define NOTES_INPUT_LEAVE	5U

/*
 * One sample of a stroke, in page coordinates.
 *
 * The position is in points with the origin at the page's top left and y
 * growing down; it is kept on the 1/64 point grid the edit data uses, so
 * that saving and reading back gives the same stroke.
 */
struct notes_point {
	float x;
	float y;
	uint16_t pressure;
	int16_t tilt_x;
	int16_t tilt_y;
	uint32_t time_ms;
};

/*
 * One stroke of ink: the samples and the tool they were drawn with.
 *
 * A stroke belongs to one page, or to an undo entry while it is off the
 * page.  Its outline, the polygon both the screen and the PDF fill, is made
 * from the samples by libpdf's pdf_outline_stroke() and kept until the
 * samples change.
 */
struct notes_stroke {
	/* The stroke's number, unique in its document and never reused. */
	uint32_t id;

	/* The tool (NOTES_TOOL_*), its colour as 0xRRGGBBAA and its full width in points. */
	unsigned tool;
	uint32_t color;
	float width;

	/* Whether the samples carry the pen's tilt, and when the stroke started (UNIX milliseconds). */
	int has_tilt;
	uint64_t start_ms;

	/* The samples, in the order they were drawn. */
	struct notes_point *points;
	size_t point_count;
	size_t point_capacity;

	/* The box the outline lies in (left, top, right, bottom, points); valid with the outline. */
	float bounds[4];

	/* The outline polygon in points (NULL until made), and its corner count. */
	struct pdf_point *outline;
	size_t outline_count;
};

/*
 * One page: its size, its background and its strokes, bottom first.
 *
 * content_hash is the SHA-256 of the page's content stream as the PDF
 * last saved or opened it (all zero: not known).  The edit data records
 * it, and opening a PDF compares it with the page in the file to learn
 * whether another program changed the page (design-pdf.md section 3).
 */
struct notes_page {
	float width;
	float height;
	unsigned background;
	unsigned char content_hash[32];
	struct notes_stroke **strokes;
	size_t stroke_count;
	size_t stroke_capacity;
};

/* The kinds of undo entry. */
#define NOTES_UNDO_ADD_STROKE		1U
#define NOTES_UNDO_REMOVE_STROKES	2U
#define NOTES_UNDO_ADD_PAGE		3U
#define NOTES_UNDO_ERASE_PARTS		4U

/*
 * One change the undo history can take back.
 *
 * An added stroke is named by its page and its place on the page.  Removed
 * strokes are kept with the places they were removed from, in the order
 * they were removed.  owned says whether the entry holds the strokes (or
 * the page) now: the strokes of a removal while it stands, the stroke of an
 * addition or the page of a page's addition while they are taken back.
 *
 * An eraser drag that cuts strokes (NOTES_UNDO_ERASE_PARTS) is kept as the
 * primitive changes it made, in order: each stroke taken off a page and
 * each piece put in its place, with the place and, in inserted, which of
 * the two it was.  Taking it back undoes them in the opposite order.  While
 * it stands (owned) the entry holds the strokes it took off; while it is
 * taken back it holds the pieces it had put in.
 */
struct notes_undo {
	unsigned kind;
	size_t page;
	size_t place;
	struct notes_stroke **strokes;
	size_t *places;
	unsigned char *inserted;
	size_t count;
	size_t capacity;
	struct notes_page *page_held;
	int owned;
};

struct notes_journal;

/*
 * A notebook: its pages and the history of its changes.
 *
 * One lives for as long as Notes shows it.  Every change goes through
 * document.c, which first tells the journal (when there is one) and then
 * applies it, so the journal always ends with the change the document has
 * just made.
 */
struct notes_document {
	/* The pages, in order. */
	struct notes_page **pages;
	size_t page_count;
	size_t page_capacity;

	/* The number the next stroke gets, and when the document was created (UNIX milliseconds). */
	uint32_t next_id;
	uint64_t time_base;

	/* The PDF's permanent identifier once the document has been saved (design-pdf.md section 1). */
	unsigned char pdf_id[16];
	int has_pdf_id;

	/*
	 * The undo history: the entries in order, how many stand (the rest
	 * were taken back and can be redone), and how many there are.
	 */
	struct notes_undo *undo;
	size_t undo_done;
	size_t undo_count;

	/*
	 * The eraser drag: 0 when none is under way, 1 while one has removed
	 * nothing yet, 2 once its removals gather in the last entry of the history.
	 */
	int erasing;

	/* A change since the last save; cleared by a save. */
	int dirty;

	/*
	 * Counts the changes other than a stroke put on top of a page: a
	 * stroke inserted below others or removed, a page inserted or removed.
	 * It only ever grows.  The screen keeps the finished strokes of a page
	 * in a picture and adds the strokes put on top to it; a new count tells
	 * it that the picture must be drawn again from the start.
	 */
	uint64_t reshaped;

	/* The journal every change is logged to (NULL: none). */
	struct notes_journal *journal;
};

/*
 * One input event, from a pointer or a pen, in the surface's pixels.
 *
 * The pointer gives a fixed pressure; the tablet protocol (ws079-p003)
 * gives the pen's pressure and tilt through the same event.
 */
struct notes_input {
	unsigned kind;
	unsigned source;
	float x;
	float y;
	float pressure;
	float tilt_x;
	float tilt_y;
	uint32_t time_ms;
};

/* A growable byte buffer: the edit data and the journal's records are built in one. */
struct notes_buffer {
	unsigned char *data;
	size_t length;
	size_t capacity;
	int error;
};

/* The document (document.c). */
int notes_document_init(struct notes_document *document, uint64_t time_base);
void notes_document_free(struct notes_document *document);
struct notes_stroke *notes_stroke_create(uint32_t id, unsigned tool, uint32_t color, float width, uint64_t start_ms);
void notes_stroke_free(struct notes_stroke *stroke);
int notes_stroke_append(struct notes_stroke *stroke, const struct notes_point *point);
int notes_stroke_outline(struct notes_stroke *stroke);
float notes_quantize(float value);
struct notes_page *notes_page_create(float width, float height, unsigned background);
void notes_page_free(struct notes_page *page);
int notes_document_insert_page(struct notes_document *document, size_t index, struct notes_page *page);
struct notes_page *notes_document_remove_page(struct notes_document *document, size_t index);
int notes_document_insert_stroke(struct notes_document *document, size_t page, size_t place, struct notes_stroke *stroke);
struct notes_stroke *notes_document_remove_stroke(struct notes_document *document, size_t page, uint32_t id, size_t *place);
int notes_document_add_stroke(struct notes_document *document, size_t page, struct notes_stroke *stroke);
int notes_document_add_page(struct notes_document *document, size_t index);
void notes_document_erase_begin(struct notes_document *document);
int notes_document_erase_at(struct notes_document *document, size_t page, float x, float y, float radius, size_t *removed);
int notes_document_erase_parts_at(struct notes_document *document, size_t page, float x, float y, float radius, size_t *cut);
void notes_document_erase_end(struct notes_document *document);
int notes_document_undo(struct notes_document *document, size_t *page);
int notes_document_redo(struct notes_document *document, size_t *page);
size_t notes_document_stroke_total(const struct notes_document *document);

/* The byte buffer (encode.c). */
void notes_buffer_init(struct notes_buffer *buffer);
void notes_buffer_free(struct notes_buffer *buffer);
void notes_buffer_bytes(struct notes_buffer *buffer, const void *data, size_t length);
void notes_buffer_u8(struct notes_buffer *buffer, unsigned value);
void notes_buffer_u16(struct notes_buffer *buffer, unsigned value);
void notes_buffer_u32(struct notes_buffer *buffer, uint32_t value);
void notes_buffer_u64(struct notes_buffer *buffer, uint64_t value);
void notes_buffer_varint(struct notes_buffer *buffer, uint64_t value);
void notes_buffer_zigzag(struct notes_buffer *buffer, int64_t value);

/* The edit data (encode.c). */
int notes_encode_document(const struct notes_document *document, struct notes_buffer *buffer);
int notes_decode_document(const void *data, size_t size, struct notes_document *document);
int notes_encode_stroke(const struct notes_stroke *stroke, struct notes_buffer *buffer);
int notes_decode_stroke(const unsigned char *data, size_t size, size_t *used, struct notes_stroke **stroke);

/* The journal (journal.c). */
int notes_journal_path(const char *document_path, char *path, size_t size);
int notes_journal_folder(char *path, size_t size);
struct notes_journal *notes_journal_create(const char *document_path);
void notes_journal_destroy(struct notes_journal *journal);
const char *notes_journal_document_path(const struct notes_journal *journal);
int notes_journal_add_stroke(struct notes_journal *journal, const struct notes_document *document, size_t page, size_t place, const struct notes_stroke *stroke);
int notes_journal_remove_stroke(struct notes_journal *journal, const struct notes_document *document, size_t page, uint32_t id);
int notes_journal_add_page(struct notes_journal *journal, const struct notes_document *document, size_t index);
int notes_journal_remove_page(struct notes_journal *journal, const struct notes_document *document, size_t index);
int notes_journal_discard(struct notes_journal *journal);
int notes_journal_recover(const char *journal_path, struct notes_document *document, char *document_path, size_t size, size_t *records);
int notes_journal_newest(char *path, size_t size);

/* The PDF (save.c). */
int notes_save_pdf(struct notes_document *document, const char *path, size_t *bytes);
int notes_open_pdf(const char *path, struct notes_document *document);

#endif
