/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p005: the host test of inserted texts, on make-edit-samples.py's
 * edit-text.pdf:
 *
 *   host-edit-insert-text IN OUT BLANK
 *
 * Inserts "Hello wrapping world" in Sans, 10 points, red, wrapped at 50
 * points, at 150,120, and Japanese and a second line (a break) in CJK at
 * 150,150; both are text objects of their boxes, the wrapped one more than
 * a line high; the preview draws.  The update saved to OUT opens again
 * with the words as lines of text.  A blank page of 200 by 100 with a text
 * inserted, drawn on a new document's page saved to BLANK, opens with it.
 * Prints each check and exits 0 when all passed.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static size_t find_line(struct pdf_page_editor *editor, const char *text);

int
main(
	int argc,
	char **argv)
{
	static const double at_left[6] = { 1.0, 0.0, 0.0, 1.0, 150.0, 120.0 };
	static const double below[6] = { 1.0, 0.0, 0.0, 1.0, 150.0, 150.0 };
	static const double corner[6] = { 1.0, 0.0, 0.0, 1.0, 20.0, 20.0 };
	struct pdf_document *document;
	struct pdf_document *saved;
	struct pdf_page_editor *editor;
	struct pdf_display_list *list;
	struct pdf_edit_object object;
	struct pdf_edit_text words;
	struct pdf_writer *writer;
	size_t index;
	size_t pages;
	unsigned result;
	int error;

	/* The sample's page. */
	if (argc != 4)
		return 2;
	error = pdf_document_open(argv[1], &document);
	if (error == 0)
		error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "open edit-text.pdf and its page's editor");
	if (error != 0)
		return 1;

	/* A wrapped text in Sans, red. */
	memset(&words, 0, sizeof(words));
	words.size = sizeof(words);
	words.utf8 = "Hello wrapping world";
	words.font = PDF_EDIT_FONT_SANS;
	words.font_size = 10.0;
	words.red = 1.0;
	words.box_width = 50.0;
	error = pdf_page_editor_insert_text(editor, &words, at_left, &index, &result);
	check(error == 0 && result == PDF_EDIT_TEXT_REPLACED && index + 1U == pdf_page_editor_count(editor), "a wrapped text inserted, the last object");
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, index, &object);
	check(error == 0 && object.kind == PDF_EDIT_TEXT && (object.flags & PDF_EDIT_OBJECT_INSERTED) != 0U && strcmp(object.text, "Hello wrapping world") == 0,
	      "it is an inserted text of its words");
	check(fabs(object.quad[0] - 150.0) < 0.01 && fabs(object.quad[1] - 120.0) < 0.01 && fabs(object.quad[2] - 200.0) < 0.01 && object.quad[7] - object.quad[1] > 20.0,
	      "its box: 150,120, 50 wide, more than a line high");

	/* Japanese and a second line in CJK. */
	words.utf8 = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\nsecond";
	words.font = PDF_EDIT_FONT_CJK;
	words.font_size = 12.0;
	words.red = 0.0;
	words.box_width = 0.0;
	error = pdf_page_editor_insert_text(editor, &words, below, &index, &result);
	check(error == 0 && result == PDF_EDIT_TEXT_REPLACED, "Japanese and a second line inserted in CJK");
	error = pdf_page_editor_object(editor, index, &object);
	check(error == 0 && fabs(object.quad[7] - object.quad[1] - 2.0 * 12.0 * 1.2) < 0.01, "its box: two lines high");
	words.font_size = 0.0;
	check(pdf_page_editor_insert_text(editor, &words, below, &index, &result) == EINVAL, "a text of no size: EINVAL");

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

	/* Opened again: the words as lines. */
	error = pdf_document_open(argv[2], &saved);
	if (error == 0)
		error = pdf_page_editor_open(saved, 0, &editor);
	check(error == 0, "the saved file and its editor");
	if (error == 0) {
		check(find_line(editor, "Hello") != (size_t)-1, "saved: Hello on a line of its own (wrapped)");
		check(find_line(editor, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") != (size_t)-1 && find_line(editor, "second") != (size_t)-1, "saved: the Japanese line and the second");
		pdf_page_editor_close(editor);
		pdf_document_close(saved);
	}

	/* A blank page with a text, a new document's page. */
	error = pdf_page_editor_blank(200.0, 100.0, &editor);
	words.utf8 = "Blank page";
	words.font = PDF_EDIT_FONT_MONO;
	words.font_size = 14.0;
	if (error == 0)
		error = pdf_page_editor_insert_text(editor, &words, corner, &index, &result);
	if (error == 0)
		error = pdf_writer_create(&writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 200.0, 100.0);
	if (error == 0)
		error = pdf_writer_draw_page_editor(writer, editor);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, argv[3]);
	check(error == 0, "a blank page with a text saved as a new document");
	if (error == 0)
		pdf_writer_destroy(writer);
	pdf_page_editor_close(editor);
	error = pdf_document_open(argv[3], &saved);
	if (error == 0)
		error = pdf_page_editor_open(saved, 0, &editor);
	pages = 0;
	if (error == 0)
		pages = pdf_document_page_count(saved);
	check(error == 0 && pages == 1 && find_line(editor, "Blank page") != (size_t)-1, "saved: the blank page's text");
	if (error == 0) {
		pdf_page_editor_close(editor);
		pdf_document_close(saved);
	}

	/* The summary. */
	printf("host-edit-insert-text: %d passed, %d failed\n", test_passed, test_failed);
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
