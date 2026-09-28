/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PDF library of the base programs (libpdf, plan/ws079/design-pdf.md).
 *
 * The writer produces PDF 1.7 documents made of filled vector paths and
 * attached files.  It depends on nothing but the C library, and knows
 * neither the window system nor the renderer.
 */

#ifndef _PDF_H_
#define _PDF_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The rule that decides which parts of a self-intersecting path are inside.
 */
enum pdf_fill_rule {
	PDF_FILL_NONZERO = 0,
	PDF_FILL_EVEN_ODD = 1
};

/*
 * A document being written.
 *
 * It holds every finished page's content in memory until the document is
 * saved, and lives from pdf_writer_create() to pdf_writer_destroy().
 */
struct pdf_writer;

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
int pdf_writer_attach_file(struct pdf_writer *writer, const char *name, const char *mime_type, const void *data, size_t size);
int pdf_writer_save(struct pdf_writer *writer, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* _PDF_H_ */
