/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p003b: the host test of the editor's images, on make-edit-samples.py's
 * edit-images.pdf and a small JPEG:
 *
 *   host-edit-image IN JPEG OUT
 *
 * page 1: the form replaced by a translucent 4 by 2 RGBA image (fitted into
 * the form's 30 by 30 corners: 30 by 15, centred), the 8 by 4 JPEG
 * inserted at 50,70 to 90,90, the hit and the preview (four images where
 * they go), the refusals (a CMYK JPEG, RGBA bytes of the wrong length);
 * an update saved with them to OUT, which opens again with four objects and
 * four images drawn.  Prints each check and exits 0 when all passed.
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
static size_t count_images(const struct pdf_display_list *list);
static int has_image_at(const struct pdf_display_list *list, double x, double y);
static unsigned char *read_file(const char *path, size_t *size);

int
main(
	int argc,
	char **argv)
{
	static const double inserted[6] = { 40.0, 0.0, 0.0, -20.0, 50.0, 90.0 };
	static const unsigned char rgba[32] = {
		255, 0, 0, 128, 255, 0, 0, 128, 255, 0, 0, 128, 255, 0, 0, 128,
		0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255
	};
	struct pdf_document *document;
	struct pdf_document *saved;
	struct pdf_page_editor *editor;
	struct pdf_display_list *list;
	struct pdf_edit_object object;
	struct pdf_image_source source;
	struct pdf_writer *writer;
	unsigned char *jpeg;
	size_t jpeg_size;
	size_t images;
	size_t index;
	size_t pages;
	double dx;
	double dy;
	int error;

	/* The sample and the JPEG. */
	if (argc != 4)
		return 2;
	jpeg = read_file(argv[2], &jpeg_size);
	error = pdf_document_open(argv[1], &document);
	check(error == 0 && jpeg != NULL, "open edit-images.pdf and the JPEG");
	if (error != 0 || jpeg == NULL)
		return 1;
	error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "page 1: the editor opens");
	if (error != 0)
		return 1;

	/* The form replaced by the RGBA image, fitted 30 by 15 into its 30 by 30 corners, centred. */
	memset(&source, 0, sizeof(source));
	source.size = sizeof(source);
	source.kind = PDF_IMAGE_SOURCE_RGBA;
	source.data = rgba;
	source.bytes = sizeof(rgba);
	source.width = 4;
	source.height = 2;
	error = pdf_page_editor_set_image(editor, 1, &source);
	check(error == 0, "the form replaced by a 4 by 2 RGBA image");
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 1, &object);
	dx = fabs(object.quad[0] - 100.0) + fabs(object.quad[2] - 130.0);
	dy = fabs(object.quad[1] - 77.5) + fabs(object.quad[5] - 92.5);
	check(error == 0 && object.kind == PDF_EDIT_IMAGE && object.image_width == 4 && dx < 0.01 && dy < 0.01, "the image at 100,77.5 to 130,92.5");

	/* The refusals. */
	source.bytes = sizeof(rgba) - 1;
	check(pdf_page_editor_set_image(editor, 1, &source) == EINVAL, "RGBA bytes of the wrong length: EINVAL");
	source.kind = PDF_IMAGE_SOURCE_JPEG;
	source.data = jpeg;
	source.bytes = jpeg_size;
	source.width = 8;
	source.height = 4;
	source.components = 4;
	check(pdf_page_editor_set_image(editor, 1, &source) == EINVAL, "a CMYK JPEG: EINVAL");

	/* The JPEG inserted at 50,70 to 90,90, the last object, and hit there. */
	source.components = 3;
	error = pdf_page_editor_insert_image(editor, &source, inserted, &index);
	check(error == 0 && index == 3 && pdf_page_editor_count(editor) == 4, "the JPEG inserted, object 3");
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 3, &object);
	check(error == 0 && (object.flags & PDF_EDIT_OBJECT_INSERTED) != 0U && fabs(object.quad[0] - 50.0) < 0.01 && fabs(object.quad[1] - 70.0) < 0.01, "the inserted image's top left at 50,70");
	error = pdf_page_editor_hit(editor, 60.0, 80.0, &index);
	check(error == 0 && index == 3, "60,80 is the inserted image");

	/* The preview: the image, the new image, the inline image and the inserted one. */
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	images = 0;
	if (error == 0) {
		images = count_images(list);
		check(has_image_at(list, 100.0, 77.5), "preview: the new image's top left at 100,77.5");
		check(has_image_at(list, 50.0, 70.0), "preview: the inserted image's top left at 50,70");
		pdf_display_list_destroy(list);
	}

	/* Four. */
	check(error == 0 && images == 4, "preview: four images");

	/* The update with the images, the other pages kept. */
	error = pdf_writer_create_update(document, &writer);
	if (error == 0)
		error = pdf_writer_begin_page_edited(writer, editor);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	pages = pdf_document_page_count(document);
	for (index = 1; error == 0 && index < pages; index++)
		error = pdf_writer_keep_page(writer, index);
	if (error == 0)
		error = pdf_writer_save(writer, argv[3]);
	check(error == 0, "the update with the images saved");
	pdf_writer_destroy(writer);
	pdf_page_editor_close(editor);

	/* Opened again: four objects of page 1, four images drawn, the new ones where they went. */
	error = pdf_document_open(argv[3], &saved);
	check(error == 0, "open the saved file");
	if (error == 0) {
		error = pdf_page_editor_open(saved, 0, &editor);
		check(error == 0 && pdf_page_editor_count(editor) == 4, "saved page 1: four objects");
		pdf_page_editor_close(editor);
		error = pdf_page_render(saved, 0, &list);
		images = 0;
		if (error == 0) {
			images = count_images(list);
			check(has_image_at(list, 100.0, 77.5) && has_image_at(list, 50.0, 70.0), "saved page 1: the new images where they went");
			pdf_display_list_destroy(list);
		}

		/* Four drawn. */
		check(error == 0 && images == 4, "saved page 1 drawn: four images");
		pdf_document_close(saved);
	}

	/* The document and the JPEG go. */
	pdf_document_close(document);
	free(jpeg);

	/* The summary. */
	printf("host-edit-image: %d passed, %d failed\n", test_passed, test_failed);
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

/* Reads a whole file; NULL when it cannot. */
static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;
	size_t got;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	if (length <= 0) {
		fclose(file);
		return NULL;
	}

	/* Its bytes. */
	data = malloc((size_t)length);
	got = 0;
	if (data != NULL)
		got = fread(data, 1, (size_t)length, file);
	fclose(file);
	if (data == NULL || got != (size_t)length) {
		free(data);
		return NULL;
	}

	/* Succeeded: the bytes. */
	*size = got;
	return data;
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
