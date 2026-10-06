/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p004: the host test of the rewriting of lines of text, on
 * make-edit-samples.py's edit-text.pdf:
 *
 *   host-edit-text-change IN OUT
 *
 * "Line one" moved 20 to the right, "Quote" twice as large about its
 * start, "Next" (a ') deleted, "Line two" given "Changed!" in its own
 * embedded font, the Identity-H line given its words twice; Helvetica (not
 * embedded) and Japanese in WinAnsiEncoding need a replacement font; the
 * invisible line takes no words.  A reset puts a moved line back.  The
 * update saved to OUT opens again with the lines where the changes put
 * them and every other line where it was -- the leading a TD set and the
 * spacing a " set still hold (design.md [H1]).  Prints each check and
 * exits 0 when all passed.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

/* The most lines the page has. */
#define TEST_LINES	16

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static size_t find_line(struct pdf_page_editor *editor, const char *text);
static size_t line_at(struct pdf_page_editor *editor, double x, double y);
static int same_quad(const double one[8], const double other[8], double dx);

int
main(
	int argc,
	char **argv)
{
	static const char *const names[] = { "Line one", "Line two", "Line three", "After TL", "Quote", "Next", "Dq", "Helvetica" };
	static const double across[6] = { 1.0, 0.0, 0.0, 1.0, 20.0, 0.0 };
	struct pdf_document *document;
	struct pdf_document *saved;
	struct pdf_page_editor *editor;
	struct pdf_display_list *list;
	struct pdf_edit_object object;
	struct pdf_edit_text words;
	struct pdf_writer *writer;
	double quads[TEST_LINES][8];
	double larger[6];
	double width;
	char doubled[64];
	size_t lines[TEST_LINES];
	size_t wide;
	size_t at;
	unsigned result;
	int error;

	/* The sample and its lines. */
	if (argc != 3)
		return 2;
	error = pdf_document_open(argv[1], &document);
	if (error == 0)
		error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "open edit-text.pdf and its page's editor");
	if (error != 0)
		return 1;
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	for (at = 0; at < sizeof(names) / sizeof(names[0]); at++) {
		lines[at] = find_line(editor, names[at]);
		error = pdf_page_editor_object(editor, lines[at], &object);
		memcpy(quads[at], object.quad, sizeof(quads[at]));
		check(lines[at] != (size_t)-1 && error == 0, names[at]);
	}

	/* The Identity-H line, at 10,170 as shown (y downward), and its words twice. */
	wide = line_at(editor, 12.0, 168.0);
	error = pdf_page_editor_object(editor, wide, &object);
	check(wide != (size_t)-1 && error == 0 && strlen(object.text) == 1, "the Identity-H line: one character");
	(void)snprintf(doubled, sizeof(doubled), "%s%s", object.text, object.text);

	/* Moved, sized, deleted, given words. */
	check(pdf_page_editor_place(editor, lines[0], across) == 0, "Line one moved 20 to the right");
	larger[0] = 2.0;
	larger[1] = 0.0;
	larger[2] = 0.0;
	larger[3] = 2.0;
	larger[4] = -quads[4][6];
	larger[5] = -quads[4][7];
	check(pdf_page_editor_place(editor, lines[4], larger) == 0, "Quote twice as large about its start");
	check(pdf_page_editor_delete(editor, lines[5]) == 0, "Next deleted");
	memset(&words, 0, sizeof(words));
	words.size = sizeof(words);
	words.utf8 = "Changed!";
	error = pdf_page_editor_set_text(editor, lines[1], &words, &result);
	check(error == 0 && result == PDF_EDIT_TEXT_ORIGINAL, "Line two given \"Changed!\" in its own font");
	words.utf8 = doubled;
	error = pdf_page_editor_set_text(editor, wide, &words, &result);
	check(error == 0 && result == PDF_EDIT_TEXT_ORIGINAL, "the Identity-H line given its words twice");

	/* What its own font cannot write. */
	words.utf8 = "Arial";
	error = pdf_page_editor_set_text(editor, lines[7], &words, &result);
	check(error == ENOTSUP && result == PDF_EDIT_TEXT_NEEDS_FONT, "Helvetica (not embedded): needs a replacement font");
	words.utf8 = "\xe6\x97\xa5\xe6\x9c\xac";
	error = pdf_page_editor_set_text(editor, lines[2], &words, &result);
	check(error == ENOTSUP && result == PDF_EDIT_TEXT_NEEDS_FONT, "Japanese in WinAnsiEncoding: needs a replacement font");
	words.utf8 = "two\nlines";
	check(pdf_page_editor_set_text(editor, lines[2], &words, &result) == EINVAL, "words with a line break: EINVAL");

	/* A reset puts a moved line back. */
	check(pdf_page_editor_place(editor, lines[3], across) == 0 && pdf_page_editor_reset(editor, lines[3]) == 0, "After TL moved and reset");
	error = pdf_page_editor_object(editor, lines[3], &object);
	check(error == 0 && same_quad(object.quad, quads[3], 0.0), "After TL where it was");

	/* The preview draws. */
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	check(error == 0, "the preview drawn");
	if (error == 0)
		pdf_display_list_destroy(list);

	/* The update saved. */
	error = pdf_writer_create_update(document, &writer);
	if (error == 0)
		error = pdf_writer_begin_page_edited(writer, editor);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, argv[2]);
	check(error == 0, "the update saved");
	pdf_writer_destroy(writer);
	pdf_page_editor_close(editor);
	pdf_document_close(document);

	/* Opened again: the changes where they put the lines, every other line where it was. */
	error = pdf_document_open(argv[2], &saved);
	if (error == 0)
		error = pdf_page_editor_open(saved, 0, &editor);
	check(error == 0, "the saved file and its editor");
	if (error != 0)
		return 1;
	at = find_line(editor, "Line one");
	error = pdf_page_editor_object(editor, at, &object);
	check(at != (size_t)-1 && error == 0 && same_quad(object.quad, quads[0], 20.0), "saved: Line one 20 to the right");
	at = find_line(editor, "Changed!");
	error = pdf_page_editor_object(editor, at, &object);
	check(at != (size_t)-1 && error == 0 && fabs(object.quad[0] - quads[1][0]) < 0.01 && fabs(object.quad[1] - quads[1][1]) < 0.01, "saved: \"Changed!\" where Line two started");
	check(find_line(editor, "Line two") == (size_t)-1, "saved: Line two is gone");
	check(find_line(editor, "Next") == (size_t)-1, "saved: Next is gone");
	at = find_line(editor, doubled);
	check(at != (size_t)-1, "saved: the Identity-H line's words twice");
	at = find_line(editor, "Quote");
	error = pdf_page_editor_object(editor, at, &object);
	width = object.quad[2] - object.quad[0];
	check(at != (size_t)-1 && error == 0 && fabs(width - 2.0 * (quads[4][2] - quads[4][0])) < 0.05 && fabs(object.quad[6] - quads[4][6]) < 0.01 &&
	      fabs(object.quad[7] - quads[4][7]) < 0.01, "saved: Quote twice as wide from where it started");
	for (at = 2; at < sizeof(names) / sizeof(names[0]); at++) {
		if (at == 4 || at == 5)
			continue;
		lines[at] = find_line(editor, names[at]);
		error = pdf_page_editor_object(editor, lines[at], &object);
		check(lines[at] != (size_t)-1 && error == 0 && same_quad(object.quad, quads[at], 0.0), names[at]);
	}

	/* What was opened goes. */
	pdf_page_editor_close(editor);
	pdf_document_close(saved);

	/* The summary. */
	printf("host-edit-text-change: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Finds the line of a text; (size_t)-1 when there is none. */
static size_t
find_line(
	struct pdf_page_editor *editor,
	const char *text)
{
	struct pdf_edit_object object;
	size_t count;
	size_t at;
	int differs;
	int error;

	/* Each line. */
	count = pdf_page_editor_count(editor);
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	for (at = 0; at < count; at++) {
		error = pdf_page_editor_object(editor, at, &object);
		if (error != 0 || object.kind != PDF_EDIT_TEXT)
			continue;
		differs = strcmp(object.text, text);
		if (differs == 0)
			return at;
	}

	/* None. */
	return (size_t)-1;
}

/* Finds the line at a point; (size_t)-1 when there is none. */
static size_t
line_at(
	struct pdf_page_editor *editor,
	double x,
	double y)
{
	size_t index;
	int error;

	/* The object on top there. */
	error = pdf_page_editor_hit(editor, x, y, &index);
	if (error != 0)
		return (size_t)-1;
	return index;
}

/* Tells whether two corners are the same, the first moved by dx across. */
static int
same_quad(
	const double one[8],
	const double other[8],
	double dx)
{
	size_t at;
	double expected;

	/* Each number. */
	for (at = 0; at < 8; at++) {
		expected = other[at];
		if (at % 2U == 0U)
			expected += dx;
		if (fabs(one[at] - expected) > 0.01)
			return 0;
	}

	/* All the same. */
	return 1;
}

/* Counts and prints one check. */
static void
check(
	int ok,
	const char *what)
{
	/* A check that holds. */
	if (ok) {
		test_passed++;
		printf("ok   %s\n", what);
		return;
	}

	/* One that does not. */
	test_failed++;
	printf("FAIL %s\n", what);
}
