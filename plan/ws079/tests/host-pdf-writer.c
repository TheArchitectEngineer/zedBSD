/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libpdf's writer (ws079-p004).
 *
 * It writes a two-page document: page 1 holds pressure strokes that
 * pdf_outline_stroke() outlines (an opaque wave whose pressure rises and
 * falls, a translucent one crossing it, and a dot), page 2 a hand-built
 * outline, and the document carries an attached edit-data file, a fixed
 * identifier and fixed dates, so two runs write the same bytes.  It then
 * checks the refusals.  run-pdf-writer.sh validates the file with qpdf and
 * pdfinfo.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pdf.h>

/* The number of samples of each test wave. */
#define TEST_WAVE_POINTS 80

static int write_document(const char *path);
static int draw_pressure_wave(struct pdf_writer *writer, double top, double alpha, double phase);
static int draw_dot(struct pdf_writer *writer);
static int draw_wedge(struct pdf_writer *writer, double x, double y, double alpha);
static int check_refusals(void);

/*
 * Writes the document named by the first argument, then checks the refusals.
 */
int
main(
	int argc,
	char **argv)
{
	int error;

	/* Needs the output path. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-pdf-writer out.pdf\n");
		return 2;
	}

	/* Writes the document. */
	error = write_document(argv[1]);
	if (error != 0) {
		fprintf(stderr, "host-pdf-writer: writing failed: %s\n", strerror(error));
		return 1;
	}

	/* Checks the refusals. */
	error = check_refusals();
	if (error != 0) {
		fprintf(stderr, "host-pdf-writer: a refusal failed: %s\n", strerror(error));
		return 1;
	}

	/* Succeeded: the document is written and every misuse was refused. */
	printf("host-pdf-writer: ok\n");
	return 0;
}

/* Writes the test document to a path. */
static int
write_document(
	const char *path)
{
	static const unsigned char edit_data[] = { 'Z', 'N', 'O', 'T', 1, 0, 0, 0, 0, 0, 0, 0, '(', ')', '\\', 0xff };
	static const unsigned char document_id[16] = {
		0x7a, 0x65, 0x64, 0x42, 0x53, 0x44, 0x2d, 0x70, 0x30, 0x30, 0x34, 0x2d, 0x74, 0x65, 0x73, 0x74
	};
	unsigned char read_back[16];
	struct pdf_writer *writer;
	int error;

	/* Creates the document. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;

	/* Fixes the identifier and the dates (2026-09-28 03:00:00 and 12:00:00 UTC), so the output is reproducible. */
	error = pdf_writer_set_document_id(writer, document_id);
	if (error == 0)
		error = pdf_writer_set_dates(writer, 1790564400, 1790596800);
	if (error == 0)
		error = pdf_writer_get_document_id(writer, read_back);
	if (error == 0 && memcmp(read_back, document_id, sizeof(document_id)) != 0)
		error = EPROTO;

	/* Draws page 1: an opaque pressure wave, a translucent one over it and a dot. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.276, 841.89);
	if (error == 0)
		error = draw_pressure_wave(writer, 120.0, 1.0, 0.0);
	if (error == 0)
		error = draw_pressure_wave(writer, 150.0, 0.4, 1.5);
	if (error == 0)
		error = draw_dot(writer);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Draws page 2, which starts opaque again and reuses the translucent state. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.276, 841.89);
	if (error == 0)
		error = draw_wedge(writer, 300.0, 400.0, 0.4);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Attaches the edit data, whose bytes include every character a string escapes. */
	if (error == 0)
		error = pdf_writer_attach_file(writer, "zedbsd-notes.bin", "application/x-zedbsd-notes", edit_data, sizeof(edit_data));

	/* Saves the document. */
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);

	/* Reports the first failure. */
	if (error != 0)
		return error;

	/* Succeeded: the document is written. */
	return 0;
}

/* Draws a sine wave whose pressure rises from nothing to full and falls back, as a pen stroke. */
static int
draw_pressure_wave(
	struct pdf_writer *writer,
	double top,
	double alpha,
	double phase)
{
	struct pdf_stroke_point points[TEST_WAVE_POINTS];
	struct pdf_point *outline;
	size_t outline_count;
	double progress;
	int index;
	int error;

	/* Samples the wave across the page; the pressure follows a half sine along it. */
	for (index = 0; index < TEST_WAVE_POINTS; index++) {
		progress = (double)index / (double)(TEST_WAVE_POINTS - 1);
		points[index].x = 60.0 + 470.0 * progress;
		points[index].y = top + 40.0 * sin(phase + progress * 4.0 * 3.14159265358979);
		points[index].pressure = sin(progress * 3.14159265358979);
	}

	/* Outlines the stroke with a full width of 14 points. */
	error = pdf_outline_stroke(points, TEST_WAVE_POINTS, 14.0, &outline, &outline_count);
	if (error != 0)
		return error;

	/* Fills the outline in blue with the given opacity. */
	error = pdf_writer_set_fill_color(writer, 0.1, 0.2, 0.8, alpha);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, outline, outline_count);
	pdf_outline_free(outline);
	if (error != 0)
		return error;

	/* Succeeded: the stroke is drawn. */
	return 0;
}

/* Draws a tap of the pen, one point at full pressure repeated, which is a round dot. */
static int
draw_dot(
	struct pdf_writer *writer)
{
	struct pdf_stroke_point points[3];
	struct pdf_point *outline;
	size_t outline_count;
	int index;
	int error;

	/* Samples the pen three times without moving. */
	for (index = 0; index < 3; index++) {
		points[index].x = 300.0;
		points[index].y = 300.0;
		points[index].pressure = 1.0;
	}

	/* Outlines the tap with a full width of 20 points. */
	error = pdf_outline_stroke(points, 3, 20.0, &outline, &outline_count);
	if (error != 0)
		return error;

	/* Fills the dot in red. */
	error = pdf_writer_set_fill_color(writer, 0.8, 0.1, 0.1, 1.0);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, outline, outline_count);
	pdf_outline_free(outline);
	if (error != 0)
		return error;

	/* Succeeded: the dot is drawn. */
	return 0;
}

/* Draws a hand-built wedge closed by a round cap, with the path calls one by one. */
static int
draw_wedge(
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

	/* Succeeded: the wedge is drawn. */
	return 0;
}

/* Checks that the writer and the outline refuse drawing outside a page, bad values and a second attachment. */
static int
check_refusals(void)
{
	struct pdf_stroke_point bad_point;
	struct pdf_point *outline;
	struct pdf_writer *writer;
	unsigned char id[16];
	size_t outline_count;
	int error;

	/* Creates an empty document. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;

	/* Expects each misuse to be refused with EINVAL, EEXIST or ENOENT. */
	error = 0;
	if (pdf_writer_get_document_id(writer, id) != ENOENT)
		error = EPROTO;
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
	if (pdf_writer_fill_outline(writer, NULL, 2) != EINVAL)
		error = EPROTO;
	if (pdf_writer_set_dates(writer, -1, 0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_attach_file(writer, "a", "bad type", "", 0) != EINVAL)
		error = EPROTO;
	if (pdf_writer_attach_file(writer, "a", "text/plain", "", 0) != 0)
		error = EPROTO;
	if (pdf_writer_attach_file(writer, "b", "text/plain", "", 0) != EEXIST)
		error = EPROTO;
	pdf_writer_destroy(writer);

	/* Expects the outline to refuse an empty stroke, a bad width and a pressure that is not a number. */
	bad_point.x = 1.0;
	bad_point.y = 1.0;
	bad_point.pressure = 0.5;
	if (pdf_outline_stroke(&bad_point, 0, 1.0, &outline, &outline_count) != EINVAL)
		error = EPROTO;
	if (pdf_outline_stroke(&bad_point, 1, 0.0, &outline, &outline_count) != EINVAL)
		error = EPROTO;
	bad_point.pressure = sqrt(-1.0);
	if (pdf_outline_stroke(&bad_point, 1, 1.0, &outline, &outline_count) != EINVAL)
		error = EPROTO;

	/* Reports a misuse that was accepted. */
	if (error != 0)
		return error;

	/* Succeeded: every misuse was refused. */
	return 0;
}
