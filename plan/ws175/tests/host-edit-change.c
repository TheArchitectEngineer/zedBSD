/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p003a: the host test of the editor's changes and their saving, on
 * make-edit-samples.py's edit-images.pdf:
 *
 *   host-edit-change IN OUT
 *
 * page 1: the image deleted and the inline image moved 10 points right,
 * the preview drawn (the deleted image's item gone, the moved one's matrix
 * moved, an object hidden), the object's corners reported where they go;
 * an update with page 1 edited and a box drawn over it, the other pages
 * kept, saved to OUT, which starts with IN's bytes and opens again with
 * page 1's objects as edited; a read-only page refuses changes (EPERM);
 * page 5 names an image KeiIm0, so the update's names take another
 * prefix.  Prints each check and exits 0 when all passed.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"
#include "writer.h"

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static size_t count_images(const struct pdf_display_list *list);
static int has_image_at(const struct pdf_display_list *list, double x, double y);
static int same_start(const char *in, const char *out);

int
main(
	int argc,
	char **argv)
{
	static const double move[6] = { 1.0, 0.0, 0.0, 1.0, 10.0, 0.0 };
	struct pdf_document *document;
	struct pdf_document *saved;
	struct pdf_page_editor *editor;
	struct pdf_page_editor *other;
	struct pdf_display_list *list;
	struct pdf_edit_object object;
	struct pdf_writer *writer;
	size_t images;
	size_t index;
	size_t pages;
	int error;

	/* The sample. */
	if (argc != 3)
		return 2;
	error = pdf_document_open(argv[1], &document);
	check(error == 0, "open edit-images.pdf");
	if (error != 0)
		return 1;

	/* Page 1 as it is: three images drawn (the image, the form's own, the inline one). */
	error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "page 1: the editor opens");
	if (error != 0)
		return 1;
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	images = 0;
	if (error == 0) {
		images = count_images(list);
		pdf_display_list_destroy(list);
	}

	/* Three before the changes. */
	check(error == 0 && images == 3, "page 1 unchanged: three images in the preview");

	/* The image deleted, the inline image moved 10 points right. */
	error = pdf_page_editor_delete(editor, 0);
	check(error == 0, "delete the image");
	error = pdf_page_editor_place(editor, 2, move);
	check(error == 0, "move the inline image 10 points right");
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 0, &object);
	check(error == 0 && (object.flags & PDF_EDIT_OBJECT_DELETED) != 0U, "the image says it is deleted");
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 2, &object);
	check(error == 0 && fabs(object.quad[0] - 160.0) < 0.01 && fabs(object.quad[1] - 30.0) < 0.01, "the inline image's corner is at 160,30");

	/* The preview: two images, the inline one at its new place; the form hidden leaves one. */
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	images = 0;
	if (error == 0) {
		images = count_images(list);
		check(has_image_at(list, 160.0, 30.0), "preview: the inline image's top left at 160,30");
		check(!has_image_at(list, 10.0, 40.0), "preview: the deleted image is not drawn");
		pdf_display_list_destroy(list);
	}

	/* Two after them, one with the form hidden too. */
	check(error == 0 && images == 2, "preview: two images");
	error = pdf_page_editor_render(editor, 1, &list);
	images = 0;
	if (error == 0) {
		images = count_images(list);
		pdf_display_list_destroy(list);
	}

	/* One. */
	check(error == 0 && images == 1, "preview with the form hidden: one image");

	/* Undo by reset: the image back. */
	error = pdf_page_editor_reset(editor, 0);
	check(error == 0, "reset the image");
	error = pdf_page_editor_delete(editor, 0);
	check(error == 0, "delete it again");

	/* A read-only page refuses changes. */
	error = pdf_page_editor_open(document, 2, &other);
	if (error == 0) {
		check(pdf_page_editor_delete(other, 0) == EPERM, "page 3 (a stream not read): delete is EPERM");
		pdf_page_editor_close(other);
	}

	/* The update: page 1 edited with a box drawn over it, the others kept. */
	error = pdf_writer_create_update(document, &writer);
	check(error == 0, "create the update");
	if (error != 0)
		return 1;
	check(strcmp(writer->name_prefix, "Kei1_") == 0, "page 5 uses KeiIm0: the update's names are Kei1_");
	error = pdf_writer_begin_page_edited(writer, editor);
	check(error == 0, "page 1 listed edited");
	(void)pdf_writer_set_fill_color(writer, 0.0, 0.0, 1.0, 1.0);
	(void)pdf_writer_move_to(writer, 5.0, 5.0);
	(void)pdf_writer_line_to(writer, 25.0, 5.0);
	(void)pdf_writer_line_to(writer, 25.0, 25.0);
	(void)pdf_writer_close_path(writer);
	error = pdf_writer_fill(writer, PDF_FILL_NONZERO);
	check(error == 0, "a box drawn over page 1");
	error = pdf_writer_end_page(writer);
	pages = pdf_document_page_count(document);
	for (index = 1; error == 0 && index < pages; index++)
		error = pdf_writer_keep_page(writer, index);
	check(error == 0, "the other pages kept");
	error = pdf_writer_save(writer, argv[2]);
	check(error == 0, "save the update");
	pdf_writer_destroy(writer);
	pdf_page_editor_close(editor);
	check(same_start(argv[1], argv[2]), "the saved file starts with the document's own bytes");

	/* The saved file's page 1: the form and the moved inline image, the box over them. */
	error = pdf_document_open(argv[2], &saved);
	check(error == 0, "open the saved file");
	if (error == 0) {
		error = pdf_page_editor_open(saved, 0, &editor);
		check(error == 0 && pdf_page_editor_count(editor) == 2, "saved page 1: two objects (the image deleted)");
		memset(&object, 0, sizeof(object));
		object.size = sizeof(object);
		if (error == 0)
			error = pdf_page_editor_object(editor, 1, &object);
		check(error == 0 && object.kind == PDF_EDIT_IMAGE && fabs(object.quad[0] - 160.0) < 0.01, "saved page 1: the inline image at 160,30");
		check(pdf_page_editor_status(editor) == 0U, "saved page 1: editable, every object listed");
		pdf_page_editor_close(editor);
		error = pdf_page_render(saved, 0, &list);
		images = 0;
		if (error == 0) {
			images = count_images(list);
			pdf_display_list_destroy(list);
		}

		/* Drawn: the two. */
		check(error == 0 && images == 2, "saved page 1 drawn: two images");
		pdf_document_close(saved);
	}

	/* The document goes. */
	pdf_document_close(document);

	/* The summary. */
	printf("host-edit-change: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
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

/* Tells whether an image item's top left is at a point. */
static int
has_image_at(
	const struct pdf_display_list *list,
	double x,
	double y)
{
	double dx;
	double dy;
	size_t i;

	/* Each image's (0, 0) corner. */
	for (i = 0; i < list->count; i++) {
		if (list->items[i].type != PDF_ITEM_IMAGE)
			continue;
		dx = fabs(list->items[i].matrix[4] - x);
		dy = fabs(list->items[i].matrix[5] - y);
		if (dx < 0.01 && dy < 0.01)
			return 1;
	}

	/* None there. */
	return 0;
}

/* Tells whether a file starts with another's bytes. */
static int
same_start(
	const char *in,
	const char *out)
{
	FILE *first;
	FILE *second;
	int a;
	int b;
	int same;

	/* Both files. */
	first = fopen(in, "rb");
	second = fopen(out, "rb");
	same = first != NULL && second != NULL;

	/* Byte by byte to the first file's end. */
	while (same) {
		a = fgetc(first);
		if (a == EOF)
			break;
		b = fgetc(second);
		if (a != b)
			same = 0;
	}

	/* Closed. */
	if (first != NULL)
		fclose(first);
	if (second != NULL)
		fclose(second);
	return same;
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
