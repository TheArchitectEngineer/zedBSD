/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PDF library of the base programs (libpdf, plan/ws079/design-pdf.md).
 *
 * The writer produces PDF 1.7 documents made of filled vector paths, images and
 * attached files.  pdf_outline_stroke() turns a pen stroke into the outline
 * polygon that both the writer and a screen renderer fill.  The reader opens
 * the documents the writer produces: their pages, page boxes, content hashes
 * and attached files.  pdf_page_render() interprets a page's content into a
 * display list of fills, images and clips (stage 1 of design-pdf.md: paths,
 * strokes, colours, opacity and the Multiply blend, images), which a program
 * draws itself or has pdf_display_list_rasterize() draw into memory.  An
 * update (pdf_writer_create_update()) adds a revision to a document being
 * read -- pages drawn over, kept or added, an attached file -- and saves it
 * after the document's own bytes, which stay as they were.  The reader
 * also reads general PDF (stages 2 and 3: text in embedded and substituted
 * fonts, shadings, cross-reference and object streams, the filters,
 * encryption with the empty, the user's or the owner's password).  The
 * library depends on the C library, libz-compat, libjpeg-compat and
 * libtruetype, and knows neither the window system nor the renderer.
 *
 * Every call that can fail reports 0 or an errno value: EINVAL for a misuse,
 * ENOMEM when memory or a limit of the reader runs out, PDF_EFORMAT for a
 * malformed PDF, PDF_EPASSWORD for an encrypted one the password does not
 * open, and ENOTSUP for a valid PDF that uses a feature the reader does not
 * read (another security handler, a document that cannot be read at all).
 */

#ifndef _PDF_H_
#define _PDF_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The error a malformed PDF reports.
 *
 * The C library has no errno value for a file of the wrong format, so the
 * library reports an illegal byte sequence.
 */
#define PDF_EFORMAT EILSEQ

/*
 * The error an update reports for a signed document, which a new revision
 * could invalidate.
 */
#define PDF_ESIGNED EPERM

/*
 * The error the reader reports for an encrypted document opened with a
 * password that is neither its user password nor its owner password (by
 * pdf_document_open(), the empty one).  pdf_document_encrypted() tells it
 * apart from a file the system does not let the program read, and
 * pdf_document_open_password() takes the password a person types.
 */
#define PDF_EPASSWORD EACCES

/*
 * How an update draws over a page of the document it adds to.
 *
 * OVERLAY keeps the page's own content and draws the new content over it;
 * REPLACE draws the new content instead of the page's own.
 */
enum pdf_page_use {
	PDF_PAGE_OVERLAY = 0,
	PDF_PAGE_REPLACE = 1
};

/*
 * The rule that decides which parts of a self-intersecting path are inside.
 */
enum pdf_fill_rule {
	PDF_FILL_NONZERO = 0,
	PDF_FILL_EVEN_ODD = 1
};

/*
 * One sampled point of a pen stroke.
 *
 * The position is in page points; the pressure runs from 0 (the lightest
 * touch) to 1 (the device's maximum).
 */
struct pdf_stroke_point {
	double x;
	double y;
	double pressure;
};

/*
 * One corner of an outline polygon, in page points.
 */
struct pdf_point {
	double x;
	double y;
};

/*
 * The boxes of one page of a document being read.
 *
 * The media box and the crop box are in the page's own coordinates (points,
 * y upward), with left < right and bottom < top; the crop box is clipped to
 * the media box.  rotation is how far the page is turned clockwise when
 * shown: 0, 90, 180 or 270.  width and height are the size of the crop box
 * as shown, after the rotation.
 */
struct pdf_page_box {
	double media_left;
	double media_bottom;
	double media_right;
	double media_top;
	double crop_left;
	double crop_bottom;
	double crop_right;
	double crop_top;
	int rotation;
	double width;
	double height;
};

/*
 * How a fill or an image is combined with what is under it.
 */
enum pdf_blend_mode {
	PDF_BLEND_NORMAL = 0,
	PDF_BLEND_MULTIPLY = 1
};

/*
 * What one item of a display list does.
 */
enum pdf_item_type {
	PDF_ITEM_FILL = 0,
	PDF_ITEM_IMAGE = 1,
	PDF_ITEM_CLIP_PUSH = 2,
	PDF_ITEM_CLIP_POP = 3
};

/*
 * The steps of a path: a move and a line take one point, a cubic Bezier
 * curve takes three (two control points and its end), a close takes none.
 */
enum pdf_path_verb {
	PDF_PATH_MOVE = 0,
	PDF_PATH_LINE = 1,
	PDF_PATH_CUBIC = 2,
	PDF_PATH_CLOSE = 3
};

/* A display list's flags: content left out, content cut short by an error, and by a limit. */
#define PDF_DISPLAY_SKIPPED 0x1U
#define PDF_DISPLAY_DAMAGED 0x2U
#define PDF_DISPLAY_LIMITED 0x4U

/*
 * One thing a page draws, in the page's shown space: points, the origin at
 * the top left of the crop box as shown, y downward, the page's rotation
 * applied.
 *
 * A fill paints its path (verbs and points) by its rule in its colour
 * (red, green, blue from 0 to 1, not premultiplied) at its alpha, combined
 * by its blend mode; a stroked line of the page arrives as the fill of its
 * outline.  An image paints its pixels (RGBA, 8 bits a channel, not
 * premultiplied, the first row the top one) over the parallelogram matrix
 * maps the unit square onto: x = a u + c v + e, y = b u + d v + f with
 * matrix {a, b, c, d, e, f}, (0, 0) the first pixel's corner and (1, 1)
 * the last's; its alpha and blend mode apply as a fill's.  A clip push
 * intersects the clip with its path (by its rule) until the matching clip
 * pop.  Only the fields of the item's type are meaningful.
 */
struct pdf_display_item {
	enum pdf_item_type type;
	enum pdf_fill_rule rule;
	const unsigned char *verbs;
	size_t verb_count;
	const struct pdf_point *points;
	size_t point_count;
	double red;
	double green;
	double blue;
	double alpha;
	enum pdf_blend_mode blend;
	const unsigned char *pixels;
	size_t image_width;
	size_t image_height;
	double matrix[6];
	int interpolate;
};

/*
 * The drawing of one page: its shown size in points, its items in the
 * order they are drawn, and the PDF_DISPLAY_* flags that say whether
 * anything of the page could not be drawn.
 *
 * It owns everything its items point at and lives from pdf_page_render()
 * to pdf_display_list_destroy(), independently of its document.
 */
struct pdf_display_list {
	double width;
	double height;
	unsigned flags;
	size_t count;
	const struct pdf_display_item *items;
};

/*
 * A document being written.
 *
 * It holds every finished page's content in memory until the document is
 * saved, and lives from pdf_writer_create() to pdf_writer_destroy().
 */
struct pdf_writer;

/*
 * A document being read.
 *
 * It holds the file's bytes and every object read from them, and lives from
 * pdf_document_open() or pdf_document_open_memory() (or their password forms) to
 * pdf_document_close().  One document is not used by two threads at once.
 */
struct pdf_document;

/* The writer: pages of filled paths and images, an attached file, the identifier and the dates. */
int pdf_writer_create(struct pdf_writer **writer);
void pdf_writer_destroy(struct pdf_writer *writer);
int pdf_writer_begin_page(struct pdf_writer *writer, double width, double height);
int pdf_writer_end_page(struct pdf_writer *writer);
int pdf_writer_set_fill_color(struct pdf_writer *writer, double red, double green, double blue, double alpha);
int pdf_writer_move_to(struct pdf_writer *writer, double x, double y);
int pdf_writer_line_to(struct pdf_writer *writer, double x, double y);
int pdf_writer_curve_to(struct pdf_writer *writer, double x1, double y1, double x2, double y2, double x3, double y3);
int pdf_writer_close_path(struct pdf_writer *writer);
int pdf_writer_fill(struct pdf_writer *writer, enum pdf_fill_rule rule);
int pdf_writer_fill_outline(struct pdf_writer *writer, const struct pdf_point *outline, size_t count);
int pdf_writer_draw_rgba_image(struct pdf_writer *writer, const unsigned char *pixels, size_t width, size_t height, double x, double y, double draw_width, double draw_height);
int pdf_writer_draw_jpeg_image(struct pdf_writer *writer, const void *data, size_t size, size_t width, size_t height, int components, double x, double y, double draw_width, double draw_height);
int pdf_writer_attach_file(struct pdf_writer *writer, const char *name, const char *mime_type, const void *data, size_t size);
int pdf_writer_set_document_id(struct pdf_writer *writer, const unsigned char id[16]);
int pdf_writer_get_document_id(const struct pdf_writer *writer, unsigned char id[16]);
int pdf_writer_set_dates(struct pdf_writer *writer, time_t creation, time_t modification);
int pdf_writer_get_page_content_hash(const struct pdf_writer *writer, size_t index, unsigned char digest[32]);
int pdf_writer_save(struct pdf_writer *writer, const char *path);

/* A writer that adds a revision to a document being read, and the pages of that document it lists. */
int pdf_writer_create_update(struct pdf_document *base, struct pdf_writer **writer);
int pdf_writer_keep_page(struct pdf_writer *writer, size_t index);
int pdf_writer_begin_page_over(struct pdf_writer *writer, size_t index, enum pdf_page_use use);

/* The outline of a pen stroke, which the writer and a screen fill alike. */
int pdf_outline_stroke(const struct pdf_stroke_point *points, size_t count, double width, struct pdf_point **outline, size_t *outline_count);
void pdf_outline_free(struct pdf_point *outline);

/* The reader: a document opened from a file or memory, its pages, its attached file and what its trailer says. */
int pdf_document_open(const char *path, struct pdf_document **document);
int pdf_document_open_memory(const void *data, size_t size, struct pdf_document **document);
int pdf_document_open_password(const char *path, const char *password, struct pdf_document **document);
int pdf_document_open_memory_password(const void *data, size_t size, const char *password, struct pdf_document **document);
void pdf_document_close(struct pdf_document *document);
size_t pdf_document_page_count(const struct pdf_document *document);
int pdf_document_page_box(struct pdf_document *document, size_t index, struct pdf_page_box *box);
int pdf_document_page_content_hash(struct pdf_document *document, size_t index, unsigned char digest[32]);
int pdf_document_find_attachment(struct pdf_document *document, const char *name, const void **data, size_t *size);
int pdf_document_find_attachment_type(struct pdf_document *document, const char *name, const char *mime_type, const void **data, size_t *size);
int pdf_document_get_id(const struct pdf_document *document, unsigned char id[16]);
int pdf_document_get_dates(const struct pdf_document *document, time_t *creation, time_t *modification);
int pdf_document_get_revision(const struct pdf_document *document, size_t *xref_offset, size_t *previous_offset);
int pdf_document_signed(struct pdf_document *document, int *is_signed);
int pdf_document_encrypted(const char *path, int *encrypted);

/*
 * The editor of a page's objects (ws175, plan/ws175/phase001/design.md
 * section 3): the images and graphics (form XObjects) of the page's
 * content, at its top level, listed in the order they are drawn
 * (ws175-p002; the text lines come later).  An object is named by a key --
 * its kind, the place and length of its bytes in the page's decoded
 * content, and a fingerprint of them -- that stays the same when the
 * document is opened again.
 */
enum pdf_edit_kind {
	PDF_EDIT_TEXT = 0,
	PDF_EDIT_IMAGE = 1,
	PDF_EDIT_GRAPHIC = 2
};

/* An object's flags: a clip is in force where it is drawn; the editor deleted it (ws175-p003). */
#define PDF_EDIT_OBJECT_CLIPPED	0x1U
#define PDF_EDIT_OBJECT_DELETED	0x2U

/*
 * The page's state (pdf_page_editor_status): a content stream could not
 * be decoded (SKIPPED), went past a limit (LIMITED) or is malformed
 * (DAMAGED) -- the page cannot be edited, since writing its content again
 * would lose what was not read -- or some objects are not listed (PARTIAL:
 * past the limit of q's nesting, or after the content stopped).
 */
#define PDF_EDIT_PAGE_SKIPPED	0x1U
#define PDF_EDIT_PAGE_LIMITED	0x2U
#define PDF_EDIT_PAGE_DAMAGED	0x4U
#define PDF_EDIT_PAGE_PARTIAL	0x8U

/* The flags that keep a page from being edited. */
#define PDF_EDIT_PAGE_READ_ONLY	(PDF_EDIT_PAGE_SKIPPED | PDF_EDIT_PAGE_LIMITED | PDF_EDIT_PAGE_DAMAGED)

/* An object's key: its kind, the offset and length of its bytes in the decoded content, and the first bytes of their SHA-256. */
struct pdf_edit_key {
	enum pdf_edit_kind kind;
	uint64_t offset;
	uint32_t length;
	unsigned char fingerprint[8];
};

/*
 * An object as the editor shows it.  size is the caller's sizeof (fields
 * added later are left alone for an older caller).  quad is the corners it
 * covers in the page's shown space (points, the top left the origin, y
 * downward): an image's top left, top right, bottom right and bottom left
 * as it is drawn.  text, font_name and font_size are a text line's (empty
 * for the others); image_width and image_height an image's samples.
 */
struct pdf_edit_object {
	size_t size;
	enum pdf_edit_kind kind;
	unsigned flags;
	double quad[8];
	const char *text;
	char font_name[64];
	double font_size;
	size_t image_width;
	size_t image_height;
};

/* A page's objects, read from a document being read; it lives until pdf_page_editor_close(), not after its document. */
struct pdf_page_editor;

/* A page interpreted into a display list, and the list drawn into memory. */
int pdf_page_render(struct pdf_document *document, size_t index, struct pdf_display_list **list);
void pdf_display_list_destroy(struct pdf_display_list *list);
int pdf_display_list_rasterize(const struct pdf_display_list *list, uint32_t *pixels, size_t stride, size_t width, size_t height, double scale, double offset_x, double offset_y);

/* The editor of a page's objects: opened on a page, its objects, their keys, what is at a point, and the page's state. */
int pdf_page_editor_open(struct pdf_document *document, size_t index, struct pdf_page_editor **editor);
void pdf_page_editor_close(struct pdf_page_editor *editor);
unsigned pdf_page_editor_status(const struct pdf_page_editor *editor);
size_t pdf_page_editor_count(const struct pdf_page_editor *editor);
int pdf_page_editor_object(const struct pdf_page_editor *editor, size_t index, struct pdf_edit_object *object);
int pdf_page_editor_key(const struct pdf_page_editor *editor, size_t index, struct pdf_edit_key *key);
int pdf_page_editor_find(const struct pdf_page_editor *editor, const struct pdf_edit_key *key, size_t *index);
int pdf_page_editor_hit(const struct pdf_page_editor *editor, double x, double y, size_t *index);

/*
 * The changes (ws175-p003): an object put back, deleted, or moved and sized
 * by an affine map of the shown space (a point p goes to p times
 * transform); the page drawn with them (hidden, an object left out of the
 * drawing, or (size_t)-1); and an update's page written with them, its own
 * content changed, the drawing that follows going over it (the page as
 * shown, as pdf_writer_begin_page_over draws).  A page that cannot be
 * edited (PDF_EDIT_PAGE_READ_ONLY) refuses the changes with EPERM.
 */
int pdf_page_editor_reset(struct pdf_page_editor *editor, size_t index);
int pdf_page_editor_delete(struct pdf_page_editor *editor, size_t index);
int pdf_page_editor_place(struct pdf_page_editor *editor, size_t index, const double transform[6]);
int pdf_page_editor_render(struct pdf_page_editor *editor, size_t hidden, struct pdf_display_list **list);
int pdf_writer_begin_page_edited(struct pdf_writer *writer, const struct pdf_page_editor *editor);

#ifdef __cplusplus
}
#endif

#endif /* _PDF_H_ */
