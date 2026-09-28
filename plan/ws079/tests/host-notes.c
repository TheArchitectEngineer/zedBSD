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
 * the PDF the run script checks with qpdf and pdftoppm, and opens it again
 * (the same document).  The edit data is also written to OUTPUT.pdf.bin.
 * ws079-p011 adds the eraser of parts (check_erase_parts).  ws079-p014
 * writes on other programs' PDFs: a copy whose page was changed
 * (check_changed), a PDF without the edit data, saved as revisions of it,
 * journaled, opened again, saved again at the same size, with a revision
 * of another program after Notes' (check_foreign), and the refusal of a
 * signed and an encrypted PDF (check_refusals).
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
static void check_changed(const char *path, const struct notes_document *original);
static void check_foreign(const char *path);
static void check_refusals(const char *path);
static int starts_with(const char *path, const char *prefix_path, size_t *size);
static int write_minimal(const char *path, const char *catalog_extra, const char *trailer_extra);
static int add_third_party_revision(const char *path);
static int copy_file(const char *from, const char *to);
static unsigned char *read_all(const char *path, size_t *size);

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
	unsigned opened;
	int error;

	/* ws079-p008: an encrypted PDF the reader opens is still refused. */
	if (argc == 3 && strcmp(argv[1], "encrypted") == 0) {
		check(notes_open_pdf(argv[2], &copy, &opened) == EACCES, "encrypted PDF that opens refused");
		if (failures != 0)
			return 1;
		printf("host-notes encrypted: ok\n");
		return 0;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: host-notes OUTPUT.pdf SCRATCH.pdf | encrypted IN.pdf\n");
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
	check(notes_open_pdf(argv[1], &copy, &opened) == 0 && opened == NOTES_OPENED_NOTES, "open");
	check(same_document(&document, &copy), "opened document is the same");
	check(copy.has_pdf_id && memcmp(copy.pdf_id, document.pdf_id, 16U) == 0, "opened identifier");
	check(copy.dirty == 0, "opened clean");
	notes_document_free(&copy);

	/*
	 * ws079-p014: a page another program changed becomes background, the
	 * other page keeps its strokes (one number of page 1's content stream
	 * is altered), and a PDF without the edit data is another program's.
	 */
	check(tamper(argv[1], argv[2]) == 0, "tamper");
	check_changed(argv[2], &document);
	check(foreign(argv[2]) == 0, "foreign");
	check_foreign(argv[2]);
	check_refusals(argv[2]);

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

/*
 * ws079-p014: a notebook of which another program changed page 1 (path):
 * page 1 becomes the background (drawn over, its strokes baked into it),
 * page 2 keeps its strokes (to be written anew in place).  A stroke drawn
 * on page 1 and a save add a revision to the changed file, which the file
 * then starts with; opening it again gives the strokes back editable.
 */
static void
check_changed(
	const char *path,
	const struct notes_document *original)
{
	struct notes_document changed;
	struct notes_document again;
	struct notes_stroke *stroke;
	char work[4096];
	size_t bytes;
	size_t size;
	unsigned opened;
	int error;

	/* A working copy of the changed file. */
	snprintf(work, sizeof(work), "%s-changed.pdf", path);
	check(copy_file(path, work) == 0, "changed copy");

	/* Page 1 is background now, page 2 is the notebook's. */
	error = notes_open_pdf(work, &changed, &opened);
	check(error == 0 && opened == NOTES_OPENED_CHANGED, "changed notebook opens as changed");
	if (error != 0)
		return;
	check(changed.page_count == 2U && changed.base != NULL, "changed: two pages and a base");
	check(changed.pages[0]->origin == NOTES_ORIGIN_OVER && changed.pages[0]->stroke_count == 0U, "changed: page 1 background");
	check(changed.pages[0]->background == NOTES_BACKGROUND_PDF, "changed: page 1 drawn under");
	check(changed.pages[1]->origin == NOTES_ORIGIN_REPLACE && changed.pages[1]->source == 1U, "changed: page 2 replaced in place");
	check(changed.pages[1]->stroke_count == original->pages[1]->stroke_count, "changed: page 2 keeps its strokes");

	/* A stroke on the changed page, saved as a revision of the changed file. */
	stroke = make_stroke(&changed, NOTES_TOOL_PEN, 0x16a34aff, 4.0f, 100.0f, 400.0f, 450.0f, 420.0f, 1);
	check(notes_document_add_stroke(&changed, 0U, stroke) == 0, "changed: stroke on page 1");
	check(notes_save_pdf(&changed, work, &bytes) == 0, "changed: saved");
	check(starts_with(work, path, &size) == 0, "changed: the changed file untouched at the start");

	/* Opened again: the strokes are editable, page 2 is still the notebook's. */
	error = notes_open_pdf(work, &again, &opened);
	check(error == 0 && opened == NOTES_OPENED_ANNOTATED, "changed: opens as annotated");
	if (error == 0) {
		check(same_document(&changed, &again), "changed: same notebook");
		check(again.pages[0]->origin == NOTES_ORIGIN_OVER && again.pages[1]->origin == NOTES_ORIGIN_REPLACE, "changed: origins kept");
		check(again.base_size == (uint64_t)size, "changed: base is the changed file");
		notes_document_free(&again);
	}
	notes_document_free(&changed);
}

/*
 * ws079-p014: another program's PDF (path, one page 200x200 without edit
 * data).  It opens with its page as the background; a stroke on it and a
 * page added with a stroke are journaled (the journal recovers the same
 * notebook and gets its base back), saved as a revision of the PDF,
 * opened again editable, and saved again at the same size -- the revision
 * is replaced, not piled up.  A revision another program adds after Notes'
 * bakes the page's strokes into the background; the added page keeps its
 * strokes.  Erasing every stroke of the page keeps the page as it was.
 */
static void
check_foreign(
	const char *path)
{
	struct notes_document document;
	struct notes_document again;
	struct notes_document recovered;
	struct notes_document third;
	struct notes_stroke *stroke;
	struct notes_stroke *removed;
	char work[4096];
	char other[4096];
	char journal_path[4096];
	char document_path[4096];
	size_t first_size;
	size_t second_size;
	size_t base_size;
	size_t records;
	size_t place;
	size_t bytes;
	unsigned opened;
	int error;

	/* A working copy of the PDF. */
	snprintf(work, sizeof(work), "%s-foreign.pdf", path);
	check(copy_file(path, work) == 0, "foreign copy");

	/* The page is the background. */
	error = notes_open_pdf(work, &document, &opened);
	check(error == 0 && opened == NOTES_OPENED_FOREIGN, "foreign PDF opens");
	if (error != 0)
		return;
	check(document.page_count == 1U && document.base != NULL, "foreign: one page and a base");
	check(document.pages[0]->origin == NOTES_ORIGIN_OVER && document.pages[0]->source == 0U, "foreign: the page is the base's");
	check(document.pages[0]->width == 200.0f && document.pages[0]->height == 200.0f, "foreign: the page's size");
	check(document.pages[0]->background == NOTES_BACKGROUND_PDF, "foreign: drawn under");

	/* A stroke on the page, and a page added with a stroke, journaled. */
	document.journal = notes_journal_create(work);
	check(document.journal != NULL, "foreign: journal");
	stroke = make_stroke(&document, NOTES_TOOL_PEN, 0xdc2626ff, 3.0f, 20.0f, 100.0f, 180.0f, 110.0f, 1);
	check(notes_document_add_stroke(&document, 0U, stroke) == 0, "foreign: stroke on the page");
	check(notes_document_add_page(&document, 1U) == 0, "foreign: page added");
	stroke = make_stroke(&document, NOTES_TOOL_HIGHLIGHTER, 0xfacc1559, 12.0f, 20.0f, 60.0f, 180.0f, 60.0f, 0);
	check(notes_document_add_stroke(&document, 1U, stroke) == 0, "foreign: stroke on the added page");
	check(document.pages[1]->origin == NOTES_ORIGIN_NEW, "foreign: the added page is Notes' own");

	/* The journal recovers the same notebook, which gets its base back from the file. */
	check(notes_journal_path(work, journal_path, sizeof(journal_path)) == 0, "foreign: journal path");
	error = notes_journal_recover(journal_path, &recovered, document_path, sizeof(document_path), &records);
	check(error == 0, "foreign: journal recovers");
	if (error == 0) {
		check(same_document(&document, &recovered), "foreign: recovered the same");
		check(recovered.base_size == document.base_size && recovered.base == NULL, "foreign: recovered base size");
		check(recovered.pages[0]->origin == NOTES_ORIGIN_OVER, "foreign: recovered origin");
		check(notes_attach_base(work, &recovered) == 0 && recovered.base != NULL, "foreign: recovered base attached");
		notes_document_free(&recovered);
	}

	/* Saved: the PDF's bytes, then the revision. */
	check(notes_save_pdf(&document, work, &bytes) == 0, "foreign: saved");
	check(notes_journal_discard(document.journal) == 0, "foreign: journal discarded");
	notes_journal_destroy(document.journal);
	document.journal = NULL;
	check(starts_with(work, path, &base_size) == 0, "foreign: the PDF untouched at the start");
	first_size = bytes;
	printf("host-notes: foreign %lu bytes, saved %lu bytes\n", (unsigned long)base_size, (unsigned long)first_size);

	/* Opened again: the strokes are editable. */
	error = notes_open_pdf(work, &again, &opened);
	check(error == 0 && opened == NOTES_OPENED_ANNOTATED, "foreign: opens as annotated");
	if (error != 0) {
		notes_document_free(&document);
		return;
	}
	check(same_document(&document, &again), "foreign: same notebook");
	check(again.pages[0]->origin == NOTES_ORIGIN_OVER && again.pages[1]->origin == NOTES_ORIGIN_NEW, "foreign: origins kept");
	check(again.base_size == (uint64_t)base_size, "foreign: the base is the PDF");

	/* Saved again: the revision replaces the last one. */
	check(notes_save_pdf(&again, work, &bytes) == 0, "foreign: saved again");
	second_size = bytes;
	check(second_size == first_size, "foreign: the revision is replaced, not piled up");
	check(starts_with(work, path, &base_size) == 0, "foreign: the PDF still untouched");

	/* Another program adds a revision: the page's strokes are baked in, the added page keeps its own. */
	snprintf(other, sizeof(other), "%s-third.pdf", path);
	check(copy_file(work, other) == 0, "third-party copy");
	check(add_third_party_revision(other) == 0, "third-party revision");
	error = notes_open_pdf(other, &third, &opened);
	check(error == 0 && opened == NOTES_OPENED_CHANGED, "third party: opens as changed");
	if (error == 0) {
		check(third.page_count == 2U, "third party: two pages");
		check(third.pages[0]->origin == NOTES_ORIGIN_OVER && third.pages[0]->stroke_count == 0U, "third party: page strokes baked in");
		check(third.pages[1]->origin == NOTES_ORIGIN_REPLACE && third.pages[1]->stroke_count == 1U, "third party: added page keeps its stroke");
		notes_document_free(&third);
	}

	/* Every stroke of the page erased: the page is kept as the PDF has it. */
	removed = notes_document_remove_stroke(&again, 0U, again.pages[0]->strokes[0]->id, &place);
	check(removed != NULL, "foreign: stroke removed");
	notes_stroke_free(removed);
	check(notes_save_pdf(&again, work, &bytes) == 0 && bytes < second_size, "foreign: saved without the page's strokes");
	notes_document_free(&again);
	error = notes_open_pdf(work, &again, &opened);
	check(error == 0 && opened == NOTES_OPENED_ANNOTATED, "foreign: opens again");
	if (error == 0) {
		check(again.pages[0]->stroke_count == 0U && again.pages[1]->stroke_count == 1U, "foreign: the page has no strokes");
		notes_document_free(&again);
	}
	notes_document_free(&document);
}

/* ws079-p014: a signed PDF and an encrypted one are refused; a plain one opens. */
static void
check_refusals(
	const char *path)
{
	struct notes_document document;
	char work[4096];
	unsigned opened;
	int error;

	/* A signed PDF. */
	snprintf(work, sizeof(work), "%s-signed.pdf", path);
	check(write_minimal(work, " /AcroForm << /Fields [] /SigFlags 3 >>", "") == 0, "signed written");
	check(notes_open_pdf(work, &document, &opened) == EPERM, "signed PDF refused");

	/* An encrypted PDF. */
	snprintf(work, sizeof(work), "%s-encrypted.pdf", path);
	check(write_minimal(work, "", " /Encrypt << /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>") == 0, "encrypted written");
	check(notes_open_pdf(work, &document, &opened) == EACCES, "encrypted PDF refused");

	/* The same PDF without either opens. */
	snprintf(work, sizeof(work), "%s-minimal.pdf", path);
	check(write_minimal(work, "", "") == 0, "minimal written");
	error = notes_open_pdf(work, &document, &opened);
	check(error == 0 && opened == NOTES_OPENED_FOREIGN && document.page_count == 1U, "minimal PDF opens");
	if (error == 0)
		notes_document_free(&document);
}

/* Tells whether a file starts with the bytes of another (0 when it does), and the other's size. */
static int
starts_with(
	const char *path,
	const char *prefix_path,
	size_t *size)
{
	unsigned char *whole;
	unsigned char *prefix;
	size_t whole_size;
	size_t prefix_size;
	int result;

	whole = read_all(path, &whole_size);
	prefix = read_all(prefix_path, &prefix_size);
	result = -1;
	if (whole != NULL && prefix != NULL && whole_size > prefix_size && memcmp(whole, prefix, prefix_size) == 0)
		result = 0;
	*size = prefix_size;
	free(whole);
	free(prefix);
	return result;
}

/* Writes a one-page PDF by hand with extra catalog and trailer entries. */
static int
write_minimal(
	const char *path,
	const char *catalog_extra,
	const char *trailer_extra)
{
	static const char content[] = "0.2 0.5 0.9 rg 20 20 160 160 re f\n";
	char text[8192];
	size_t offsets[5];
	size_t length;
	size_t xref;
	FILE *file;

	length = 0;
	length += (size_t)sprintf(text + length, "%%PDF-1.7\n");
	offsets[1] = length;
	length += (size_t)sprintf(text + length, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R%s >>\nendobj\n", catalog_extra);
	offsets[2] = length;
	length += (size_t)sprintf(text + length, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
	offsets[3] = length;
	length += (size_t)sprintf(text + length, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>\nendobj\n");
	offsets[4] = length;
	length += (size_t)sprintf(text + length, "4 0 obj\n<< /Length %lu >>\nstream\n%s\nendstream\nendobj\n", (unsigned long)strlen(content), content);
	xref = length;
	length += (size_t)sprintf(text + length, "xref\n0 5\n0000000000 65535 f \n%010lu 00000 n \n%010lu 00000 n \n%010lu 00000 n \n%010lu 00000 n \n",
	    (unsigned long)offsets[1], (unsigned long)offsets[2], (unsigned long)offsets[3], (unsigned long)offsets[4]);
	length += (size_t)sprintf(text + length, "trailer\n<< /Size 5 /Root 1 0 R%s >>\nstartxref\n%lu\n%%%%EOF\n", trailer_extra, (unsigned long)xref);
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;
	fwrite(text, 1U, length, file);
	fclose(file);
	return 0;
}

/* Adds a revision of "another program" to a PDF: every page kept, the information dictionary dated again. */
static int
add_third_party_revision(
	const char *path)
{
	struct pdf_document *document;
	struct pdf_writer *writer;
	size_t pages;
	size_t page;
	int error;

	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	error = pdf_writer_create_update(document, &writer);
	if (error != 0) {
		pdf_document_close(document);
		return error;
	}
	pages = pdf_document_page_count(document);
	for (page = 0; page < pages && error == 0; page++)
		error = pdf_writer_keep_page(writer, page);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	pdf_document_close(document);
	return error;
}

/* Copies a file. */
static int
copy_file(
	const char *from,
	const char *to)
{
	unsigned char *data;
	size_t size;
	FILE *file;

	data = read_all(from, &size);
	if (data == NULL)
		return -1;
	file = fopen(to, "wb");
	if (file == NULL) {
		free(data);
		return -1;
	}
	fwrite(data, 1U, size, file);
	fclose(file);
	free(data);
	return 0;
}

/* Reads a whole file (NULL when it cannot). */
static unsigned char *
read_all(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1U);
	if (data != NULL)
		*size = fread(data, 1U, (size_t)length, file);
	fclose(file);
	return data;
}
