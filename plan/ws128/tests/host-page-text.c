/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws128-p004: libpdf's page text (pdf_page_text_open) on the WS175
 * samples (plan/ws175/tests/make-edit-samples.py): edit-basic.pdf's
 * paragraph in a subset TrueType font and its /Rotate 90 page, and
 * edit-images.pdf's lines in Helvetica.  Checks the characters, the line
 * ends, the space made between two strings apart, and the corners.
 *
 *   host-page-text FOLDER
 */

#include <pdf.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static struct pdf_document *open_file(const char *folder, const char *name);
static void lines_of(const struct pdf_page_text *text, char *out, size_t size);

/* Runs the checks. */
int
main(
	int argc,
	char **argv)
{
	struct pdf_document *document;
	struct pdf_page_text *text;
	const struct pdf_text_character *first;
	char lines[2048];
	int error;

	/* The folder of the samples. */
	if (argc != 2)
		return 2;

	/* edit-basic.pdf page 1: four lines, the first's T where its baseline is (72, 700 in the PDF; 92 down in the shown space). */
	document = open_file(argv[1], "edit-basic.pdf");
	check(document != NULL, "edit-basic.pdf opened");
	if (document == NULL)
		return 1;
	error = pdf_page_text_open(document, 0, &text);
	check(error == 0 && text != NULL, "page 1's text");
	if (error != 0)
		return 1;
	lines_of(text, lines, sizeof(lines));
	check(strcmp(lines, "The quick brown fox jumps over the lazy dog|pack my box with five dozen liquor jugs|"
			    "a third line that the test deletes|the fourth line keeps its place|") == 0, "page 1: the four lines in order");
	first = &text->characters[0];
	check(fabs(first->quad[0] - 72.0) < 0.5 && first->quad[1] < 92.0 && first->quad[5] > 92.0 && first->quad[2] > first->quad[0] + 5.0,
	      "the first character's corners: from x 72, across the baseline at y 92, a glyph wide");
	check(text->characters[1].quad[0] > first->quad[0] && fabs(text->characters[1].quad[0] - first->quad[2]) < 0.01,
	      "the next character starts where the first ends");
	pdf_page_text_close(text);

	/* Page 3 (/Rotate 90): its two lines, going down the shown page. */
	error = pdf_page_text_open(document, 2, &text);
	if (error == 0)
		lines_of(text, lines, sizeof(lines));
	check(error == 0 && strcmp(lines, "rotated page first line|rotated page second line|") == 0, "page 3: the rotated lines");
	if (error == 0)
		check(text->characters[1].quad[1] > text->characters[0].quad[1] + 1.0, "page 3: the text goes down the shown page");
	pdf_page_text_close(text);
	pdf_document_close(document);

	/* edit-images.pdf page 7: "Hello World" (two text objects on one baseline, a space between), the kerned line. */
	document = open_file(argv[1], "edit-images.pdf");
	error = -1;
	if (document != NULL)
		error = pdf_page_text_open(document, 6, &text);
	if (error == 0)
		lines_of(text, lines, sizeof(lines));
	check(error == 0 && strncmp(lines, "Hello World|Kerned text|Before|After|", 37) == 0, "edit-images.pdf page 7: the lines, a space made between strings apart");
	if (error == 0)
		pdf_page_text_close(text);
	pdf_document_close(document);

	/* The summary. */
	printf("host-page-text: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Opens a sample of the folder; NULL when it cannot. */
static struct pdf_document *
open_file(
	const char *folder,
	const char *name)
{
	struct pdf_document *document;
	char path[1024];
	int error;

	/* The path, then the document. */
	(void)snprintf(path, sizeof(path), "%s/%s", folder, name);
	error = pdf_document_open(path, &document);
	if (error != 0)
		return NULL;
	return document;
}

/* Writes a page's text as its lines, each ended by '|' (ASCII; others as '?'). */
static void
lines_of(
	const struct pdf_page_text *text,
	char *out,
	size_t size)
{
	size_t length;
	size_t at;

	/* Each character, and a bar after each line. */
	length = 0;
	for (at = 0; at < text->count && length + 3U < size; at++) {
		out[length] = '?';
		if (text->characters[at].character < 0x80U)
			out[length] = (char)text->characters[at].character;
		length++;
		if ((text->characters[at].flags & PDF_TEXT_LINE_END) != 0U) {
			out[length] = '|';
			length++;
		}
	}

	/* Ended. */
	out[length] = '\0';
}

/* Counts one check and prints a failure. */
static void
check(
	int ok,
	const char *what)
{
	/* Passed or failed. */
	if (ok) {
		test_passed++;
		printf("ok %s\n", what);
		return;
	}

	/* Failed. */
	test_failed++;
	printf("FAIL %s\n", what);
}
