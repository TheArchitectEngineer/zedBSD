/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p009: the host test of the clean copy (Save Clean Copy), on
 * make-clean-sample.py's sample put into object streams:
 *
 *   host-clean IN UPDATED CLEAN
 *
 * page 1's image ImA is deleted in an update that draws a box over the
 * page, keeps page 2 and attaches kei-notes.bin, saved to UPDATED (which
 * still holds ImA's bytes); the clean copy of UPDATED without
 * kei-notes.bin is saved to CLEAN: one cross-reference section, no object
 * stream, no ImA bytes, no attachment, two pages with their inherited
 * boxes, page 1 drawing ImB only, the resources the pages do not name
 * (ImA, F9) gone, and page 2's link still reaching page 1.  Prints each
 * check and exits 0 when all passed.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static int file_has(const char *path, const char *text);
static size_t count_images(const struct pdf_display_list *list);
static int has_key(struct pdf_document *document, struct pdf_object *resources, const char *category, const char *name);

int
main(
	int argc,
	char **argv)
{
	struct pdf_clean_counts counts;
	struct pdf_document *document;
	struct pdf_document *updated;
	struct pdf_document *clean;
	struct pdf_page_editor *editor;
	struct pdf_display_list *list;
	struct pdf_writer *writer;
	struct pdf_page_box box;
	struct pdf_object *page;
	struct pdf_object *resources;
	struct pdf_object *annots;
	struct pdf_object *annot;
	struct pdf_object *dest;
	const void *data;
	size_t size;
	size_t images;
	int error;

	/* The sample. */
	if (argc != 4)
		return 2;
	error = pdf_document_open(argv[1], &document);
	check(error == 0, "open the sample");
	if (error != 0)
		return 1;
	check(file_has(argv[1], "QZXQZX"), "the sample holds ImA's bytes");

	/* Page 1: ImA deleted, a box drawn over; page 2 kept; the edit data attached. */
	error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "page 1: the editor opens");
	if (error != 0)
		return 1;
	error = pdf_page_editor_delete(editor, 0);
	check(error == 0, "delete ImA");
	error = pdf_writer_create_update(document, &writer);
	check(error == 0, "create the update");
	if (error != 0)
		return 1;
	error = pdf_writer_begin_page_edited(writer, editor);
	(void)pdf_writer_set_fill_color(writer, 0.0, 0.0, 1.0, 1.0);
	(void)pdf_writer_move_to(writer, 5.0, 5.0);
	(void)pdf_writer_line_to(writer, 25.0, 5.0);
	(void)pdf_writer_line_to(writer, 25.0, 25.0);
	(void)pdf_writer_close_path(writer);
	if (error == 0)
		error = pdf_writer_fill(writer, PDF_FILL_NONZERO);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_keep_page(writer, 1);
	if (error == 0)
		error = pdf_writer_attach_file(writer, "kei-notes.bin", "application/octet-stream", "ZNOT", 4);
	if (error == 0)
		error = pdf_writer_save(writer, argv[2]);
	check(error == 0, "save the update");
	pdf_writer_destroy(writer);
	pdf_page_editor_close(editor);
	pdf_document_close(document);
	check(file_has(argv[2], "QZXQZX"), "the update still holds ImA's bytes");

	/* The clean copy of the update, without the edit data. */
	error = pdf_document_open(argv[2], &updated);
	check(error == 0, "open the update");
	if (error != 0)
		return 1;
	error = pdf_document_find_attachment(updated, "kei-notes.bin", &data, &size);
	check(error == 0, "the update has kei-notes.bin");
	memset(&counts, 0, sizeof(counts));
	error = pdf_document_save_clean(updated, argv[3], "kei-notes.bin", &counts);
	check(error == 0, "save the clean copy");
	printf("clean copy: objects=%lu dropped=%lu\n", (unsigned long)counts.objects, (unsigned long)counts.dropped);
	check(counts.dropped >= 4, "the clean copy left out the unused resources (ImA, F9 and others)");
	pdf_document_close(updated);

	/* The file: one section, no object stream, no ImA, no attachment's name. */
	check(!file_has(argv[3], "QZXQZX"), "the clean copy has no ImA bytes");
	check(!file_has(argv[3], "/Prev"), "the clean copy has one cross-reference section");
	check(!file_has(argv[3], "ObjStm"), "the clean copy has no object stream");

	/* The copy read back. */
	error = pdf_document_open(argv[3], &clean);
	check(error == 0, "open the clean copy");
	if (error != 0)
		return 1;
	check(pdf_document_page_count(clean) == 2, "the clean copy has two pages");
	error = pdf_document_find_attachment(clean, "kei-notes.bin", &data, &size);
	check(error != 0, "the clean copy has no kei-notes.bin");

	/* The boxes: page 1's inherited one, page 2's own. */
	error = pdf_document_page_box(clean, 0, &box);
	check(error == 0 && box.width > 199.9 && box.width < 200.1 && box.height > 99.9 && box.height < 100.1, "page 1 is 200 x 100");
	error = pdf_document_page_box(clean, 1, &box);
	check(error == 0 && box.width > 299.9 && box.width < 300.1, "page 2 is 300 x 150");

	/* Page 1 draws ImB only. */
	error = pdf_page_render(clean, 0, &list);
	images = 0;
	if (error == 0) {
		images = count_images(list);
		pdf_display_list_destroy(list);
	}

	/* One. */
	check(error == 0 && images == 1, "page 1 drawn: one image (ImB)");

	/* The resources: what the pages name stays, the rest is gone. */
	error = pdf_reader_page(clean, 0, &page, &resources);
	check(error == 0, "page 1's resources");
	check(has_key(clean, resources, "XObject", "ImB"), "page 1 keeps ImB");
	check(!has_key(clean, resources, "XObject", "ImA"), "page 1 left ImA out");
	check(has_key(clean, resources, "Font", "F1"), "page 1 keeps F1");
	check(!has_key(clean, resources, "Font", "F9"), "page 1 left F9 out");
	error = pdf_reader_page(clean, 1, &page, &resources);
	check(error == 0, "page 2's resources");
	check(has_key(clean, resources, "ExtGState", "G1"), "page 2 keeps G1");
	check(!has_key(clean, resources, "XObject", "ImB"), "page 2 left ImB out");

	/* Page 2's link reaches page 1 (object 3 of the copy). */
	error = pdf_reader_resolve_key(clean, page, "Annots", &annots);
	annot = NULL;
	if (error == 0 && annots->type == PDF_OBJECT_ARRAY && annots->count == 1)
		error = pdf_reader_resolve(clean, annots->values[0], &annot);
	dest = NULL;
	if (error == 0 && annot != NULL)
		error = pdf_reader_resolve_key(clean, annot, "Dest", &dest);
	check(error == 0 && dest != NULL && dest->type == PDF_OBJECT_ARRAY && dest->count == 2 &&
	      dest->values[0]->type == PDF_OBJECT_REFERENCE && dest->values[0]->number == 3, "page 2's link reaches page 1");
	pdf_document_close(clean);

	/* The summary. */
	printf("host-clean: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Records a check. */
static void
check(
	int ok,
	const char *what)
{
	/* Counts and prints it. */
	if (ok) {
		test_passed++;
		printf("ok %s\n", what);
	} else {
		test_failed++;
		printf("FAIL %s\n", what);
	}
}

/* Tells whether a file's bytes hold a text. */
static int
file_has(
	const char *path,
	const char *text)
{
	unsigned char *bytes;
	FILE *stream;
	size_t length;
	size_t size;
	size_t at;
	int found;

	/* The whole file. */
	stream = fopen(path, "rb");
	if (stream == NULL)
		return 0;
	bytes = malloc(16U * 1024U * 1024U);
	size = 0;
	if (bytes != NULL)
		size = fread(bytes, 1, 16U * 1024U * 1024U, stream);
	fclose(stream);

	/* The text anywhere in it. */
	found = 0;
	length = strlen(text);
	for (at = 0; bytes != NULL && at + length <= size && !found; at++)
		found = memcmp(bytes + at, text, length) == 0;
	free(bytes);
	return found;
}

/* Counts the image items of a drawing. */
static size_t
count_images(
	const struct pdf_display_list *list)
{
	size_t count;
	size_t i;

	/* Each item. */
	count = 0;
	for (i = 0; i < list->count; i++) {
		if (list->items[i].type == PDF_ITEM_IMAGE)
			count++;
	}

	/* The images. */
	return count;
}

/* Tells whether a category of resources has a name. */
static int
has_key(
	struct pdf_document *document,
	struct pdf_object *resources,
	const char *category,
	const char *name)
{
	struct pdf_object *dictionary;
	int error;

	/* The category, then the name in it. */
	error = pdf_reader_resolve_key(document, resources, category, &dictionary);
	if (error != 0 || dictionary->type != PDF_OBJECT_DICTIONARY)
		return 0;
	return pdf_object_get(dictionary, name) != NULL;
}
