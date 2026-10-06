/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p007: the host test of the blank editor, on a small JPEG:
 *
 *   host-edit-blank JPEG OUT
 *
 * A blank editor of 200 by 100 points: no objects of its own; the JPEG
 * inserted twice (id 5, at 10,20 to 50,40 and, moved by 100 across, at
 * 110,20 to 150,40) and drawn by the preview there; a writer's new page of
 * that size with them drawn (one image object of id 5) and a red square
 * over them, saved to OUT, which draws the two images where the preview
 * did and the square over them, and whose page content has a hash (a page
 * of Notes' own).  An editor of a page is not blank (EINVAL).  Prints each
 * check and exits 0 when all passed.
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
static int has_image_at(const struct pdf_display_list *list, double x, double y);
static size_t count_items(const struct pdf_display_list *list, enum pdf_item_type type);
static size_t count_text(const unsigned char *data, size_t size, const char *text);
static unsigned char *read_file(const char *path, size_t *size);

int
main(
	int argc,
	char **argv)
{
	static const double placement[6] = { 40.0, 0.0, 0.0, -20.0, 10.0, 40.0 };
	static const double across[6] = { 1.0, 0.0, 0.0, 1.0, 100.0, 0.0 };
	struct pdf_page_editor *editor;
	struct pdf_document *saved;
	struct pdf_display_list *list;
	struct pdf_image_source source;
	struct pdf_writer *writer;
	unsigned char digest[32];
	unsigned char *jpeg;
	unsigned char *file;
	size_t jpeg_size;
	size_t file_size;
	size_t index;
	int error;

	/* The JPEG, and the blank editor. */
	if (argc != 3)
		return 2;
	jpeg = read_file(argv[1], &jpeg_size);
	error = pdf_page_editor_blank(200.0, 100.0, &editor);
	check(error == 0 && jpeg != NULL && pdf_page_editor_count(editor) == 0, "a blank editor of 200 by 100 without objects");
	if (error != 0 || jpeg == NULL)
		return 1;
	check(pdf_page_editor_blank(0.0, 100.0, &editor) == EINVAL, "a blank editor without width: EINVAL");

	/* The JPEG inserted twice, the second moved by 100 across. */
	memset(&source, 0, sizeof(source));
	source.size = sizeof(source);
	source.kind = PDF_IMAGE_SOURCE_JPEG;
	source.data = jpeg;
	source.bytes = jpeg_size;
	source.width = 8;
	source.height = 4;
	source.components = 3;
	source.id = 5;
	error = pdf_page_editor_insert_image(editor, &source, placement, &index);
	if (error == 0)
		error = pdf_page_editor_insert_image(editor, &source, placement, &index);
	if (error == 0)
		error = pdf_page_editor_place(editor, index, across);
	check(error == 0 && pdf_page_editor_count(editor) == 2, "the JPEG inserted twice, the second moved");

	/* The preview. */
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	if (error == 0) {
		check(has_image_at(list, 10.0, 20.0) && has_image_at(list, 110.0, 20.0) && count_items(list, PDF_ITEM_IMAGE) == 2, "preview: the images at 10,20 and 110,20");
		pdf_display_list_destroy(list);
	}

	/* A new document's page with them, a red square over them. */
	error = pdf_writer_create(&writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 200.0, 100.0);
	if (error == 0)
		error = pdf_writer_draw_page_editor(writer, editor);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 1.0, 0.0, 0.0, 1.0);
	if (error == 0)
		error = pdf_writer_move_to(writer, 20.0, 25.0);
	if (error == 0)
		error = pdf_writer_line_to(writer, 30.0, 25.0);
	if (error == 0)
		error = pdf_writer_line_to(writer, 30.0, 35.0);
	if (error == 0)
		error = pdf_writer_close_path(writer);
	if (error == 0)
		error = pdf_writer_fill(writer, PDF_FILL_NONZERO);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_get_page_content_hash(writer, 0, digest);
	check(error == 0, "the page with the images and the square: its content has a hash");
	if (error == 0)
		error = pdf_writer_save(writer, argv[2]);
	check(error == 0, "saved");
	pdf_writer_destroy(writer);

	/* One image object of id 5. */
	file = read_file(argv[2], &file_size);
	check(file != NULL && count_text(file, file_size, "/KeiNotesImage 5") == 1, "one image object of id 5");
	free(file);

	/* Drawn: the images where the preview had them, then the square. */
	error = pdf_document_open(argv[2], &saved);
	if (error == 0)
		error = pdf_page_render(saved, 0, &list);
	if (error == 0) {
		check(has_image_at(list, 10.0, 20.0) && has_image_at(list, 110.0, 20.0) && count_items(list, PDF_ITEM_IMAGE) == 2, "saved: the images at 10,20 and 110,20");
		check(list->count == 3 && list->items[2].type == PDF_ITEM_FILL, "saved: the square over them");
		pdf_display_list_destroy(list);
		pdf_document_close(saved);
	}

	/* An editor of a page is not blank. */
	error = pdf_document_open(argv[2], &saved);
	if (error == 0) {
		pdf_page_editor_close(editor);
		error = pdf_page_editor_open(saved, 0, &editor);
		if (error == 0)
			error = pdf_writer_create(&writer);
		if (error == 0)
			error = pdf_writer_begin_page(writer, 200.0, 100.0);
		if (error == 0)
			check(pdf_writer_draw_page_editor(writer, editor) == EINVAL, "an editor of a page drawn: EINVAL");
		pdf_writer_destroy(writer);
		pdf_document_close(saved);
	}

	/* What was made goes. */
	pdf_page_editor_close(editor);
	free(jpeg);

	/* The summary. */
	printf("host-edit-blank: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Tells whether an image item's first pixel's corner is at a point. */
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

/* Counts the items of a type. */
static size_t
count_items(
	const struct pdf_display_list *list,
	enum pdf_item_type type)
{
	size_t count;
	size_t i;

	/* Each item. */
	count = 0;
	for (i = 0; i < list->count; i++) {
		if (list->items[i].type == type)
			count++;
	}

	/* The items of the type. */
	return count;
}

/* Counts the places a text is in bytes. */
static size_t
count_text(
	const unsigned char *data,
	size_t size,
	const char *text)
{
	size_t length;
	size_t count;
	size_t at;
	int differs;

	/* Each place. */
	length = strlen(text);
	count = 0;
	for (at = 0; at + length <= size; at++) {
		differs = memcmp(data + at, text, length);
		if (differs == 0)
			count++;
	}

	/* The places. */
	return count;
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
