/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p002b: the host test of the editor's lines of text, on
 * make-edit-samples.py's page 7:
 *
 *   host-edit-text FILE
 *
 * five lines: "Hello World" (two text objects on one baseline), "Kerned
 * text" (a TJ and a Tj), "Before" and "After" (a colour set between them),
 * "Hidden" (invisible, an OCR layer); the clipping text object is no line.
 * Their text, font, size and flags, their keys again, the hit (the visible
 * line over the invisible one); ws175-p004: a line deleted, the invisible
 * one not moved (ENOTSUP) nor given words (EPERM), Helvetica (not
 * embedded) given words in a replacement font (ws175-p005).
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pdf.h>

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);

int
main(
	int argc,
	char **argv)
{
	static const char *const expected[] = { "Hello World", "Kerned text", "Before", "After", "Hidden" };
	static const double across[6] = { 1.0, 0.0, 0.0, 1.0, 10.0, 0.0 };
	struct pdf_edit_text words;
	struct pdf_document *document;
	struct pdf_page_editor *editor;
	struct pdf_edit_object object;
	struct pdf_edit_key key;
	const char *got;
	char what[96];
	size_t count;
	size_t index;
	size_t at;
	unsigned result;
	int same;
	int error;

	/* Page 7. */
	if (argc != 2)
		return 2;
	error = pdf_document_open(argv[1], &document);
	if (error == 0)
		error = pdf_page_editor_open(document, 6, &editor);
	check(error == 0, "page 7: the editor opens");
	if (error != 0)
		return 1;

	/* Five lines, in the order drawn. */
	count = pdf_page_editor_count(editor);
	check(count == 5, "page 7: five lines (the clipping text object is none)");
	for (at = 0; at < count && at < 5; at++) {
		/* Its text. */
		memset(&object, 0, sizeof(object));
		object.size = sizeof(object);
		error = pdf_page_editor_object(editor, at, &object);
		got = "-";
		if (error == 0)
			got = object.text;
		same = error == 0 && object.kind == PDF_EDIT_TEXT && strcmp(object.text, expected[at]) == 0;
		(void)snprintf(what, sizeof(what), "line %u reads \"%s\" (got \"%s\")", (unsigned)at, expected[at], got);
		check(same, what);

		/* Its key finds it again. */
		error = pdf_page_editor_key(editor, at, &key);
		if (error == 0)
			error = pdf_page_editor_find(editor, &key, &index);
		check(error == 0 && index == at && key.kind == PDF_EDIT_TEXT, "its key finds the line");
	}

	/* The first line's font, size and corners. */
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 0, &object);
	check(error == 0 && strcmp(object.font_name, "Helvetica") == 0 && fabs(object.font_size - 10.0) < 0.01, "line 0: Helvetica, 10 points");
	check(error == 0 && fabs(object.quad[0] - 10.0) < 0.01 && fabs(object.quad[1] - 12.0) < 0.01, "line 0: its top left at 10,12 (y 180 up, the ascent 8)");
	check(error == 0 && (object.flags & (PDF_EDIT_OBJECT_TEXT_FIXED | PDF_EDIT_OBJECT_INVISIBLE)) == 0U, "line 0: its words known, visible");

	/* The invisible line. */
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 4, &object);
	check(error == 0 && (object.flags & PDF_EDIT_OBJECT_INVISIBLE) != 0U, "line 4: invisible (rendering mode 3)");

	/* The hit: the line at a point. */
	error = pdf_page_editor_hit(editor, 20.0, 18.0, &index);
	check(error == 0 && index == 0, "20,18 is line 0");
	error = pdf_page_editor_hit(editor, 20.0, 98.0, &index);
	check(error == 0 && index == 4, "20,98 is the invisible line (nothing visible there)");

	/* ws175-p004: a line deleted; the invisible one only deleted; Helvetica cannot write new words. */
	check(pdf_page_editor_delete(editor, 0) == 0, "line 0 deleted");
	check(pdf_page_editor_place(editor, 4, across) == ENOTSUP, "the invisible line moved: ENOTSUP");
	memset(&words, 0, sizeof(words));
	words.size = sizeof(words);
	words.utf8 = "Hi";
	check(pdf_page_editor_set_text(editor, 4, &words, &result) == EPERM, "the invisible line given words: EPERM");
	check(pdf_page_editor_set_text(editor, 1, &words, &result) == 0 && result == PDF_EDIT_TEXT_REPLACED, "Helvetica (not embedded) given words: in a replacement font (ws175-p005)");

	/* The summary. */
	pdf_page_editor_close(editor);
	pdf_document_close(document);
	printf("host-edit-text: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
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
