/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws079-p005: the host test of Notes' model, edit data, journal and PDF.
 *
 * It builds a document of pen and highlighter strokes on two pages, checks
 * undo and redo of strokes, eraser drags and pages, checks that the edit
 * data decodes to the same document, that a journal left behind by a
 * "crash" (and one whose last record was cut short) rebuilds it, and saves
 * the PDF the run script checks with qpdf and pdftoppm, opens it again
 * (the same document), and refuses a copy whose page was changed and a PDF
 * without the edit data.  The edit data is also written to OUTPUT.pdf.bin.
 * ws079-p011 adds the eraser of parts (check_erase_parts).
 *
 *   host-notes OUTPUT.pdf SCRATCH.pdf
 */

#include "notes.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what);
static void check_erase_parts(void);
static struct notes_stroke *make_stroke(struct notes_document *document, unsigned tool, uint32_t color, float width, float x0, float y0, float x1, float y1, int wave);
static int same_document(const struct notes_document *a, const struct notes_document *b);
static int tamper(const char *from, const char *to);
static int foreign(const char *path);

int
main(
	int argc,
	char **argv)
{
	struct notes_document document;
	struct notes_document copy;
	struct notes_document recovered;
	struct notes_buffer edit;
	struct notes_stroke *stroke;
	char journal_path[4096];
	char document_path[4096];
	char folder[4096];
	size_t removed;
	size_t records;
	size_t page;
	size_t bytes;
	FILE *file;
	int descriptor;
	int error;

	if (argc != 3) {
		fprintf(stderr, "usage: host-notes OUTPUT.pdf SCRATCH.pdf\n");
		return 2;
	}

	/* The journal lives in a private folder of the run. */
	snprintf(folder, sizeof(folder), "%s.data", argv[1]);
	setenv("XDG_DATA_HOME", folder, 1);

	/* A document with a journal, created at a fixed time. */
	error = notes_document_init(&document, 1790000000000ULL);
	check(error == 0, "init");
	document.journal = notes_journal_create("/home/test/Documents/Notes/test.pdf");
	check(document.journal != NULL, "journal create");
	check(document.page_count == 1U, "one page");

	/* Three pen strokes and a highlighter on page 0. */
	stroke = make_stroke(&document, NOTES_TOOL_PEN, 0x1a1a1aff, 3.0f, 60.0f, 100.0f, 520.0f, 100.0f, 1);
	check(notes_document_add_stroke(&document, 0U, stroke) == 0, "add stroke 1");
	stroke = make_stroke(&document, NOTES_TOOL_PEN, 0x1d4ed8ff, 6.0f, 60.0f, 200.0f, 520.0f, 300.0f, 0);
	check(notes_document_add_stroke(&document, 0U, stroke) == 0, "add stroke 2");
	stroke = make_stroke(&document, NOTES_TOOL_PEN, 0xdc2626ff, 1.5f, 300.0f, 400.0f, 300.0f, 700.0f, 1);
	check(notes_document_add_stroke(&document, 0U, stroke) == 0, "add stroke 3");
	stroke = make_stroke(&document, NOTES_TOOL_HIGHLIGHTER, 0xfacc1559, 14.0f, 80.0f, 150.0f, 480.0f, 150.0f, 0);
	check(notes_document_add_stroke(&document, 0U, stroke) == 0, "add highlighter");
	check(document.pages[0]->stroke_count == 4U, "four strokes");

	/* Undo takes the highlighter back; redo puts it back on top. */
	check(notes_document_undo(&document, &page) == 0 && page == 0U, "undo");
	check(document.pages[0]->stroke_count == 3U, "undo removed one");
	check(notes_document_redo(&document, &page) == 0, "redo");
	check(document.pages[0]->stroke_count == 4U && document.pages[0]->strokes[3]->tool == NOTES_TOOL_HIGHLIGHTER, "redo restored on top");
	check(notes_document_redo(&document, &page) == ENOENT, "nothing to redo");

	/* An eraser drag over the vertical red stroke, in two touches: one change to undo. */
	notes_document_erase_begin(&document);
	check(notes_document_erase_at(&document, 0U, 300.0f, 500.0f, 8.0f, &removed) == 0 && removed == 1U, "erase red");
	check(notes_document_erase_at(&document, 0U, 300.0f, 600.0f, 8.0f, &removed) == 0 && removed == 0U, "erase nothing more");
	check(notes_document_erase_at(&document, 0U, 290.0f, 100.0f, 8.0f, &removed) == 0 && removed == 1U, "erase the wave");
	notes_document_erase_end(&document);
	check(document.pages[0]->stroke_count == 2U, "two strokes left");
	check(notes_document_undo(&document, &page) == 0, "undo the drag");
	check(document.pages[0]->stroke_count == 4U, "the drag undone at once");
	check(document.pages[0]->strokes[0]->color == 0x1a1a1aff && document.pages[0]->strokes[2]->color == 0xdc2626ff, "strokes back in their places");

	/* A second page with a stroke; undo and redo of the page's addition. */
	check(notes_document_add_page(&document, 1U) == 0 && document.page_count == 2U, "add page");
	stroke = make_stroke(&document, NOTES_TOOL_PEN, 0x16a34aff, 4.0f, 100.0f, 100.0f, 400.0f, 500.0f, 1);
	check(notes_document_add_stroke(&document, 1U, stroke) == 0, "stroke on page 2");
	check(notes_document_undo(&document, &page) == 0 && page == 1U, "undo page 2 stroke");
	check(notes_document_undo(&document, &page) == 0 && document.page_count == 1U, "undo page");
	check(notes_document_redo(&document, &page) == 0 && document.page_count == 2U, "redo page");
	check(notes_document_redo(&document, &page) == 0 && document.pages[1]->stroke_count == 1U, "redo page 2 stroke");
	check(notes_document_stroke_total(&document) == 5U, "five strokes");

	/* The edit data decodes to the same document. */
	notes_buffer_init(&edit);
	check(notes_encode_document(&document, &edit) == 0, "encode");
	check(notes_decode_document(edit.data, edit.length, &copy) == 0, "decode");
	check(same_document(&document, &copy), "decoded document is the same");
	notes_document_free(&copy);
	snprintf(journal_path, sizeof(journal_path), "%s.bin", argv[1]);
	file = fopen(journal_path, "wb");
	if (file != NULL) {
		fwrite(edit.data, 1U, edit.length, file);
		fclose(file);
	}
	printf("host-notes: edit data %lu bytes\n", (unsigned long)edit.length);

	/* Damaged edit data is refused. */
	edit.data[4] = 9U;
	check(notes_decode_document(edit.data, edit.length, &copy) == EINVAL, "another major version refused");
	edit.data[4] = 1U;
	check(notes_decode_document(edit.data, edit.length / 2U, &copy) == EINVAL, "cut data refused");
	notes_buffer_free(&edit);

	/* The journal left by a "crash" (the process never discarded it) rebuilds the document. */
	check(notes_journal_path("/home/test/Documents/Notes/test.pdf", journal_path, sizeof(journal_path)) == 0, "journal path");
	check(notes_journal_recover(journal_path, &recovered, document_path, sizeof(document_path), &records) == 0, "recover");
	check(strcmp(document_path, "/home/test/Documents/Notes/test.pdf") == 0, "recovered path");
	check(same_document(&document, &recovered), "recovered document is the same");
	printf("host-notes: journal records %lu\n", (unsigned long)records);
	notes_document_free(&recovered);

	/* A record cut short by the crash is dropped, and the rest still recovers. */
	descriptor = open(journal_path, O_WRONLY | O_APPEND);
	check(descriptor >= 0, "journal open");
	check(write(descriptor, "\002\377\000\000\000abc", 8U) == 8, "torn record");
	close(descriptor);
	check(notes_journal_recover(journal_path, &recovered, document_path, sizeof(document_path), &records) == 0, "recover torn");
	check(same_document(&document, &recovered), "torn journal recovers the same");
	notes_document_free(&recovered);

	/* The newest journal is found. */
	check(notes_journal_newest(document_path, sizeof(document_path)) == 0 && strcmp(document_path, journal_path) == 0, "newest journal");

	/* A save writes the PDF and the journal is discarded. */
	check(notes_save_pdf(&document, argv[1], &bytes) == 0 && bytes > 0U, "save");
	check(document.dirty == 0 && document.has_pdf_id, "saved state");
	check(notes_journal_discard(document.journal) == 0, "discard");
	check(access(journal_path, F_OK) != 0, "journal gone");
	printf("host-notes: pdf %lu bytes\n", (unsigned long)bytes);

	/* A change after the save starts a new journal whose snapshot is the saved document. */
	check(notes_document_undo(&document, &page) == 0, "undo after save");
	check(access(journal_path, F_OK) == 0, "journal again");
	check(notes_journal_recover(journal_path, &recovered, document_path, sizeof(document_path), &records) == 0 && records == 2U, "snapshot and one record");
	check(same_document(&document, &recovered), "second journal recovers the same");
	notes_document_free(&recovered);
	check(notes_journal_discard(document.journal) == 0, "discard again");
	check(notes_document_redo(&document, &page) == 0, "redo after save");
	check(notes_journal_discard(document.journal) == 0, "discard at the end");

	/* The saved PDF opens to the same document, with its identifier. */
	check(notes_open_pdf(argv[1], &copy) == 0, "open");
	check(same_document(&document, &copy), "opened document is the same");
	check(copy.has_pdf_id && memcmp(copy.pdf_id, document.pdf_id, 16U) == 0, "opened identifier");
	check(copy.dirty == 0, "opened clean");
	notes_document_free(&copy);

	/* A page another program changed is refused: one number of page 1's content stream is altered. */
	check(tamper(argv[1], argv[2]) == 0, "tamper");
	check(notes_open_pdf(argv[2], &copy) == ESTALE, "changed page refused");

	/* A PDF without the edit data is not a Notes PDF. */
	check(foreign(argv[2]) == 0, "foreign");
	check(notes_open_pdf(argv[2], &copy) == ENOENT, "foreign PDF refused");

	notes_journal_destroy(document.journal);
	document.journal = NULL;
	notes_document_free(&document);

	/* ws079-p011: the eraser of parts. */
	check_erase_parts();

	if (failures != 0) {
		printf("host-notes: %d FAILED\n", failures);
		return 1;
	}
	printf("host-notes: ok\n");
	return 0;
}

/*
 * ws079-p011: the eraser of parts cuts a straight stroke where its circle
 * (widened by half the stroke's width) crosses it, twice in one drag; the
 * other stroke stays; undo puts the stroke back whole at its place, redo
 * cuts it again; the journal and the edit data give the same document; a
 * change after an undo drops the undone cuts (their pieces are freed, which
 * the ASan run checks).
 */
static void
check_erase_parts(void)
{
	struct notes_document parts;
	struct notes_document copy;
	struct notes_buffer edit;
	struct notes_stroke *stroke;
	char journal_path[4096];
	char document_path[4096];
	size_t records;
	size_t cut;
	size_t page;
	uint64_t reshaped;

	check(notes_document_init(&parts, 1790000000000ULL) == 0, "parts init");
	parts.journal = notes_journal_create("/home/test/Documents/Notes/parts.pdf");
	check(parts.journal != NULL, "parts journal");
	stroke = make_stroke(&parts, NOTES_TOOL_PEN, 0x1a1a1aff, 3.0f, 60.0f, 300.0f, 520.0f, 300.0f, 0);
	check(notes_document_add_stroke(&parts, 0U, stroke) == 0, "parts stroke 1");
	stroke = make_stroke(&parts, NOTES_TOOL_PEN, 0x1d4ed8ff, 3.0f, 60.0f, 500.0f, 520.0f, 500.0f, 0);
	check(notes_document_add_stroke(&parts, 0U, stroke) == 0, "parts stroke 2");
	reshaped = parts.reshaped;

	/* One drag: a cut at x 290 makes two pieces below the blue stroke, a cut at 400 splits the second. */
	notes_document_erase_begin(&parts);
	check(notes_document_erase_parts_at(&parts, 0U, 290.0f, 300.0f, 10.0f, &cut) == 0 && cut == 1U, "cut once");
	check(parts.pages[0]->stroke_count == 3U, "two pieces and the other stroke");
	check(parts.pages[0]->strokes[2]->id == 2U, "the other stroke stays on top");
	check(fabs(parts.pages[0]->strokes[0]->points[parts.pages[0]->strokes[0]->point_count - 1U].x - 278.5f) < 0.05, "first piece ends at the circle");
	check(fabs(parts.pages[0]->strokes[1]->points[0].x - 301.5f) < 0.05, "second piece starts at the circle");
	check(parts.pages[0]->strokes[0]->id > 2U && parts.pages[0]->strokes[1]->id > parts.pages[0]->strokes[0]->id, "pieces have new numbers");
	check(notes_document_erase_parts_at(&parts, 0U, 400.0f, 300.0f, 10.0f, &cut) == 0 && cut == 1U, "cut twice");
	check(notes_document_erase_parts_at(&parts, 0U, 400.0f, 100.0f, 10.0f, &cut) == 0 && cut == 0U, "nothing to cut");
	notes_document_erase_end(&parts);
	check(parts.pages[0]->stroke_count == 4U, "three pieces and the other stroke");
	check(parts.reshaped != reshaped, "the cut reshaped the page");

	/* Undo puts the stroke back whole; redo cuts it again. */
	check(notes_document_undo(&parts, &page) == 0 && page == 0U, "undo the cuts");
	check(parts.pages[0]->stroke_count == 2U && parts.pages[0]->strokes[0]->id == 1U, "stroke back whole");
	check(parts.pages[0]->strokes[0]->point_count == 61U, "all its samples");
	check(notes_document_redo(&parts, &page) == 0 && parts.pages[0]->stroke_count == 4U, "redo the cuts");

	/* The journal and the edit data give the same document. */
	check(notes_journal_path("/home/test/Documents/Notes/parts.pdf", journal_path, sizeof(journal_path)) == 0, "parts journal path");
	check(notes_journal_recover(journal_path, &copy, document_path, sizeof(document_path), &records) == 0, "parts recover");
	check(same_document(&parts, &copy), "parts recovered the same");
	notes_document_free(&copy);
	notes_buffer_init(&edit);
	check(notes_encode_document(&parts, &edit) == 0, "parts encode");
	check(notes_decode_document(edit.data, edit.length, &copy) == 0, "parts decode");
	check(same_document(&parts, &copy), "parts decoded the same");
	notes_document_free(&copy);
	notes_buffer_free(&edit);

	/* An undo, then a new stroke: the undone cuts are dropped with their pieces. */
	check(notes_document_undo(&parts, &page) == 0, "undo before a new stroke");
	stroke = make_stroke(&parts, NOTES_TOOL_PEN, 0x1a1a1aff, 3.0f, 60.0f, 700.0f, 520.0f, 700.0f, 0);
	check(notes_document_add_stroke(&parts, 0U, stroke) == 0, "new stroke drops the redo");
	check(parts.pages[0]->stroke_count == 3U, "whole stroke, the other and the new one");

	/* A cut that stands when the document goes: its entry frees the cut stroke. */
	notes_document_erase_begin(&parts);
	check(notes_document_erase_parts_at(&parts, 0U, 100.0f, 700.0f, 10.0f, &cut) == 0 && cut == 1U, "cut the new stroke");
	notes_document_erase_end(&parts);
	check(notes_journal_discard(parts.journal) == 0, "parts discard");
	notes_journal_destroy(parts.journal);
	parts.journal = NULL;
	notes_document_free(&parts);
	printf("host-notes: erase parts checked\n");
}

/* Counts and reports a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (condition)
		return;
	printf("FAIL: %s\n", what);
	failures++;
}

/* Makes a stroke from one point to another, straight or as a wave, with pressure rising and falling. */
static struct notes_stroke *
make_stroke(
	struct notes_document *document,
	unsigned tool,
	uint32_t color,
	float width,
	float x0,
	float y0,
	float x1,
	float y1,
	int wave)
{
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned index;
	float share;
	uint32_t id;

	id = document->next_id;
	document->next_id++;
	stroke = notes_stroke_create(id, tool, color, width, document->time_base + 1000U * id);
	for (index = 0; index <= 60U; index++) {
		share = (float)index / 60.0f;
		memset(&point, 0, sizeof(point));
		point.x = x0 + (x1 - x0) * share;
		point.y = y0 + (y1 - y0) * share;
		if (wave)
			point.y += 20.0f * (float)sin(share * 12.0);
		point.pressure = (uint16_t)(65535.0 * sin(share * 3.14159));
		point.time_ms = index * 8U;
		notes_stroke_append(stroke, &point);
	}
	return stroke;
}

/* Tells whether two documents have the same pages, strokes and samples. */
static int
same_document(
	const struct notes_document *a,
	const struct notes_document *b)
{
	const struct notes_stroke *left;
	const struct notes_stroke *right;
	size_t page;
	size_t stroke;
	size_t point;

	if (a->page_count != b->page_count || a->time_base != b->time_base || a->next_id != b->next_id)
		return 0;
	for (page = 0; page < a->page_count; page++) {
		if (a->pages[page]->stroke_count != b->pages[page]->stroke_count)
			return 0;
		if (a->pages[page]->width != b->pages[page]->width || a->pages[page]->height != b->pages[page]->height)
			return 0;
		for (stroke = 0; stroke < a->pages[page]->stroke_count; stroke++) {
			left = a->pages[page]->strokes[stroke];
			right = b->pages[page]->strokes[stroke];
			if (left->id != right->id || left->tool != right->tool || left->color != right->color || left->width != right->width)
				return 0;
			if (left->start_ms != right->start_ms || left->point_count != right->point_count)
				return 0;
			for (point = 0; point < left->point_count; point++) {
				if (left->points[point].x != right->points[point].x || left->points[point].y != right->points[point].y)
					return 0;
				if (left->points[point].pressure != right->points[point].pressure || left->points[point].time_ms != right->points[point].time_ms)
					return 0;
				if (left->points[point].tilt_x != right->points[point].tilt_x || left->points[point].tilt_y != right->points[point].tilt_y)
					return 0;
			}
		}
	}
	return 1;
}

/* Copies a PDF, changing one digit before the first line-to operator of page 1: a changed page. */
static int
tamper(
	const char *from,
	const char *to)
{
	static unsigned char buffer[1 << 20];
	unsigned char *mark;
	size_t size;
	FILE *file;

	file = fopen(from, "rb");
	if (file == NULL)
		return -1;
	size = fread(buffer, 1U, sizeof(buffer), file);
	fclose(file);
	mark = memchr(buffer + 1, 'l', size - 2U);
	while (mark != NULL && !(mark[-1] == ' ' && mark[1] == '\n'))
		mark = memchr(mark + 1, 'l', size - 1U - (size_t)(mark + 1 - buffer));
	if (mark == NULL)
		return -1;
	while (mark > buffer && !(*mark >= '0' && *mark <= '8'))
		mark--;
	*mark = (unsigned char)(*mark + 1);
	file = fopen(to, "wb");
	if (file == NULL)
		return -1;
	fwrite(buffer, 1U, size, file);
	fclose(file);
	return 0;
}

/* Writes a one-page PDF with a filled triangle and no edit data. */
static int
foreign(
	const char *path)
{
	struct pdf_writer *writer;
	int error;

	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	pdf_writer_begin_page(writer, 200.0, 200.0);
	pdf_writer_set_fill_color(writer, 0.2, 0.4, 0.8, 1.0);
	pdf_writer_move_to(writer, 20.0, 20.0);
	pdf_writer_line_to(writer, 180.0, 20.0);
	pdf_writer_line_to(writer, 180.0, 180.0);
	pdf_writer_close_path(writer);
	pdf_writer_fill(writer, PDF_FILL_NONZERO);
	pdf_writer_end_page(writer);
	error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	return error;
}
