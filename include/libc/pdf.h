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
 * and attached files.  The library depends on nothing but the C library, and
 * knows neither the window system nor the renderer.
 *
 * Every call that can fail reports 0 or an errno value: EINVAL for a misuse,
 * ENOMEM when memory or a limit of the reader runs out, PDF_EFORMAT for a
 * malformed PDF, and ENOTSUP for a valid PDF that uses a feature the reader
 * does not read yet (a cross-reference stream, a compressed stream,
 * encryption).
 */

#ifndef _PDF_H_
#define _PDF_H_

#include <errno.h>
#include <stddef.h>
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
 * pdf_document_open() or pdf_document_open_memory() to
 * pdf_document_close().  One document is not used by two threads at once.
 */
struct pdf_document;

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

int pdf_outline_stroke(const struct pdf_stroke_point *points, size_t count, double width, struct pdf_point **outline, size_t *outline_count);
void pdf_outline_free(struct pdf_point *outline);

int pdf_document_open(const char *path, struct pdf_document **document);
int pdf_document_open_memory(const void *data, size_t size, struct pdf_document **document);
void pdf_document_close(struct pdf_document *document);
size_t pdf_document_page_count(const struct pdf_document *document);
int pdf_document_page_box(struct pdf_document *document, size_t index, struct pdf_page_box *box);
int pdf_document_page_content_hash(struct pdf_document *document, size_t index, unsigned char digest[32]);
int pdf_document_find_attachment(struct pdf_document *document, const char *name, const void **data, size_t *size);
int pdf_document_find_attachment_type(struct pdf_document *document, const char *name, const char *mime_type, const void **data, size_t *size);
int pdf_document_get_id(const struct pdf_document *document, unsigned char id[16]);
int pdf_document_get_dates(const struct pdf_document *document, time_t *creation, time_t *modification);

#ifdef __cplusplus
}
#endif

#endif /* _PDF_H_ */
