/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libpdf's writer (ws079-p004).
 *
 * It writes a two-page document with an opaque and a translucent filled
 * stroke outline and an attached edit-data file, then checks the writer's
 * refusals.  run-pdf-writer.sh validates the file with qpdf and pdfinfo.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <pdf.h>

static int draw_stroke(struct pdf_writer *writer, double x, double y, double alpha);
static int check_refusals(void);

/*
 * Writes the document named by the first argument.
 */
int
main(
	int argc,
	char **argv)
{
	static const unsigned char edit_data[] = { 'Z', 'N', 'O', 'T', 1, 0, 0, 0, 0, 0, 0, 0, '(', ')', '\\', 0xff };
	struct pdf_writer *writer;
	int error;

	/* Needs the output path. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-pdf-writer out.pdf\n");
		return 2;
	}

	/* Creates the document. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		goto fail;

	/* Draws page 1 with an opaque stroke and a translucent one over it. */
	error = pdf_writer_begin_page(writer, 595.276, 841.89);
	if (error == 0)
		error = draw_stroke(writer, 100.0, 100.0, 1.0);
	if (error == 0)
		error = draw_stroke(writer, 120.0, 110.0, 0.4);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error != 0)
		goto fail;

	/* Draws page 2, which starts opaque again and reuses the translucent state. */
	error = pdf_writer_begin_page(writer, 595.276, 841.89);
	if (error == 0)
		error = draw_stroke(writer, 300.0, 400.0, 0.4);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error != 0)
		goto fail;

	/* Attaches the edit data, whose bytes include every character a string escapes. */
	error = pdf_writer_attach_file(writer, "zedbsd-notes.bin", "application/x-zedbsd-notes", edit_data, sizeof(edit_data));
	if (error != 0)
		goto fail;

	/* Saves the document. */
	error = pdf_writer_save(writer, argv[1]);
	if (error != 0)
		goto fail;
	pdf_writer_destroy(writer);

	/* Checks the refusals. */
	error = check_refusals();
	if (error != 0)
		goto fail;

	/* Succeeded: the document is written. */
	printf("host-pdf-writer: ok\n");
	return 0;

fail:
	fprintf(stderr, "host-pdf-writer: failed: %s\n", strerror(error));
	return 1;
}

/* Draws one stroke outline, a wedge closed by a round cap, as Notes writes a stroke. */
static int
draw_stroke(
	struct pdf_writer *writer,
	double x,
	double y,
	double alpha)
{
	int error;

	/* Selects a blue fill of the given opacity and fills the outline. */
	error = pdf_writer_set_fill_color(writer, 0.1, 0.2, 0.8, alpha);
	if (error == 0)
		error = pdf_writer_move_to(writer, x, y - 1.0);
	if (error == 0)
		error = pdf_writer_line_to(writer, x + 200.0, y - 4.0);
	if (error == 0)
		error = pdf_writer_curve_to(writer, x + 202.2, y - 4.0, x + 204.0, y - 2.2, x + 204.0, y);
	if (error == 0)
		error = pdf_writer_curve_to(writer, x + 204.0, y + 2.2, x + 202.2, y + 4.0, x + 200.0, y + 4.0);
	if (error == 0)
		error = pdf_writer_line_to(writer, x, y + 1.0);
	if (error == 0)
		error = pdf_writer_close_path(writer);
	if (error == 0)
		error = pdf_writer_fill(writer, PDF_FILL_NONZERO);
	if (error != 0)
		return error;

	/* Succeeded: the stroke is drawn. */
	return 0;
}

/* Checks that the writer refuses drawing outside a page, bad values and a second attachment. */
static int
check_refusals(void)
{
	struct pdf_writer *writer;
	int error;

	/* Creates an empty document. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;

	/* Expects each misuse to be refused with EINVAL or EEXIST. */
	error = 0;
	if (pdf_writer_move_to(writer, 0.0, 0.0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_save(writer, "/nonexistent/x.pdf") != EINVAL)
		error = EPROTO;
	if (pdf_writer_begin_page(writer, 0.0, 10.0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_begin_page(writer, 10.0, 10.0) != 0)
		error = EPROTO;
	if (pdf_writer_set_fill_color(writer, 1.5, 0.0, 0.0, 1.0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_line_to(writer, 1e9, 0.0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_attach_file(writer, "a", "bad type", "", 0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_attach_file(writer, "a", "text/plain", "", 0) != 0)
		error = EPROTO;
	if (pdf_writer_attach_file(writer, "b", "text/plain", "", 0) != EEXIST)
		error = EPROTO;
	pdf_writer_destroy(writer);

	/* Reports a misuse the writer accepted. */
	if (error != 0)
		return error;

	/* Succeeded: every misuse was refused. */
	return 0;
}
