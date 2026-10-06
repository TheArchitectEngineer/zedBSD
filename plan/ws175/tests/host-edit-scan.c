/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p002a: the host test of libpdf's scan of a page's images and
 * graphics (content.c pdf_content_scan) and of the editor that offers them
 * (editor.c), on make-edit-samples.py's edit-images.pdf:
 *
 *   host-edit-scan FILE
 *
 * page 1: an image, a form (its own image not the page's), an inline
 * image, their bytes, corners and keys, a stray Q and an open q, the hit,
 * and the corners against the render's placing of the same images; page 2
 * (rotated 90) the corners; page 3 a stream that is not read (SKIPPED);
 * page 4 an object past the limit of q's nesting left out (PARTIAL).  The
 * keys are the same when the document is opened again.  Prints each check
 * and exits 0 when all passed.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static int near_quad(const double quad[8], const double expected[8]);
static void test_page1(struct pdf_document *document, struct pdf_edit_key keys[3]);
static void test_render(struct pdf_document *document);
static void test_others(struct pdf_document *document);
static void test_again(const char *path, const struct pdf_edit_key keys[3]);

int
main(
	int argc,
	char **argv)
{
	struct pdf_document *document;
	struct pdf_edit_key keys[3];
	int error;

	/* The sample. */
	if (argc != 2)
		return 2;
	error = pdf_document_open(argv[1], &document);
	check(error == 0, "open edit-images.pdf");
	if (error != 0)
		return 1;

	/* Each part. */
	memset(keys, 0, sizeof(keys));
	test_page1(document, keys);
	test_render(document);
	test_others(document);
	pdf_document_close(document);
	test_again(argv[1], keys);

	/* The summary. */
	printf("host-edit-scan: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Page 1: the three objects, their bytes, corners and keys, the stray Q and the open q, and the hit. */
static void
test_page1(
	struct pdf_document *document,
	struct pdf_edit_key keys[3])
{
	static const double image[8] = { 10.0, 40.0, 60.0, 40.0, 60.0, 80.0, 10.0, 80.0 };
	static const double form[8] = { 100.0, 70.0, 130.0, 70.0, 130.0, 100.0, 100.0, 100.0 };
	static const double inline_image[8] = { 150.0, 30.0, 170.0, 30.0, 170.0, 40.0, 150.0, 40.0 };
	struct pdf_page_editor *editor;
	struct pdf_edit_object object;
	struct pdf_scan scan;
	unsigned char *content;
	unsigned read_flags;
	size_t size;
	size_t index;
	size_t i;
	int error;
	int same;

	/* The scan itself: the stray Q, the open q, the bytes of each object. */
	error = pdf_content_scan(document, 0, &scan, &content, &size, &read_flags);
	check(error == 0 && scan.count == 3, "page 1: three objects at the top level (the form's own image is not one)");
	check(error == 0 && scan.stray_count == 1 && scan.open_saves == 1 && !scan.in_text, "page 1: one stray Q, one q left open");
	if (error == 0 && scan.count == 3) {
		same = memcmp(content + scan.objects[0].offset, "/Im1 Do", 7) == 0 && scan.objects[0].length == 7;
		check(same, "page 1: the image's bytes are \"/Im1 Do\"");
		same = memcmp(content + scan.objects[1].offset, "/Fm1 Do", 7) == 0 && scan.objects[1].length == 7;
		check(same, "page 1: the form's bytes are \"/Fm1 Do\"");
		same = memcmp(content + scan.objects[2].offset, "BI", 2) == 0 && memcmp(content + scan.objects[2].offset + scan.objects[2].length - 2, "EI", 2) == 0;
		check(same, "page 1: the inline image's bytes are BI to EI");
		same = content[scan.stray_restores[0]] == 'Q';
		check(same, "page 1: the stray Q's offset is a Q");
	}
	pdf_scan_free(&scan);
	free(content);

	/* The editor: the kinds and corners. */
	error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "page 1: the editor opens");
	if (error != 0)
		return;
	check(pdf_page_editor_status(editor) == 0U, "page 1: editable, every object listed");
	check(pdf_page_editor_count(editor) == 3, "page 1: the editor has three objects");
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 0, &object);
	check(error == 0 && object.kind == PDF_EDIT_IMAGE && object.image_width == 2 && object.image_height == 2, "page 1: object 0 is the 2x2 image");
	check(error == 0 && near_quad(object.quad, image), "page 1: the image's corners 10,40 60,40 60,80 10,80");
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 1, &object);
	check(error == 0 && object.kind == PDF_EDIT_GRAPHIC && near_quad(object.quad, form), "page 1: object 1 is the form, its box at 100,70 to 130,100");
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 2, &object);
	check(error == 0 && object.kind == PDF_EDIT_IMAGE && near_quad(object.quad, inline_image), "page 1: object 2 is the inline image at 150,30 to 170,40");
	object.size = 4;
	error = pdf_page_editor_object(editor, 0, &object);
	check(error == EINVAL, "page 1: a size too small is EINVAL");

	/* The keys, and find. */
	for (i = 0; i < 3; i++) {
		error = pdf_page_editor_key(editor, i, &keys[i]);
		check(error == 0, "page 1: a key");
		error = pdf_page_editor_find(editor, &keys[i], &index);
		check(error == 0 && index == i, "page 1: find gives the key's object");
	}
	keys[0].fingerprint[0] ^= 1;
	error = pdf_page_editor_find(editor, &keys[0], &index);
	check(error == ENOENT, "page 1: a key with other bytes finds nothing");
	keys[0].fingerprint[0] ^= 1;
	error = pdf_page_editor_object(editor, 3, &object);
	check(error == ENOENT || error == EINVAL, "page 1: no fourth object");

	/* The hit, the top one first. */
	error = pdf_page_editor_hit(editor, 30.0, 60.0, &index);
	check(error == 0 && index == 0, "page 1: 30,60 is the image");
	error = pdf_page_editor_hit(editor, 115.0, 85.0, &index);
	check(error == 0 && index == 1, "page 1: 115,85 is the form");
	error = pdf_page_editor_hit(editor, 160.0, 35.0, &index);
	check(error == 0 && index == 2, "page 1: 160,35 is the inline image");
	error = pdf_page_editor_hit(editor, 5.0, 5.0, &index);
	check(error == ENOENT, "page 1: 5,5 is nothing");
	pdf_page_editor_close(editor);
}

/* Page 1's corners against the render: each top-level image's item maps its pixel corners to the scan's corners. */
static void
test_render(
	struct pdf_document *document)
{
	struct pdf_display_list *list;
	struct pdf_page_editor *editor;
	struct pdf_edit_object object;
	const struct pdf_display_item *item;
	double placed[8];
	double corners[8];
	size_t images;
	size_t i;
	size_t k;
	int matched;
	int error;

	/* The render, and the editor. */
	error = pdf_page_render(document, 0, &list);
	check(error == 0, "render page 1");
	if (error != 0)
		return;
	error = pdf_page_editor_open(document, 0, &editor);
	if (error != 0) {
		pdf_display_list_destroy(list);
		return;
	}

	/* Every object that is an image has an image item of the same corners. */
	images = 0;
	for (k = 0; k < pdf_page_editor_count(editor); k++) {
		/* An image of the page. */
		memset(&object, 0, sizeof(object));
		object.size = sizeof(object);
		error = pdf_page_editor_object(editor, k, &object);
		if (error != 0 || object.kind != PDF_EDIT_IMAGE)
			continue;
		images++;

		/* An item whose matrix sends the pixel corners (0,0) (1,0) (1,1) (0,1) to the object's. */
		matched = 0;
		for (i = 0; i < list->count; i++) {
			item = &list->items[i];
			if (item->type != PDF_ITEM_IMAGE)
				continue;
			corners[0] = item->matrix[4];
			corners[1] = item->matrix[5];
			corners[2] = item->matrix[0] + item->matrix[4];
			corners[3] = item->matrix[1] + item->matrix[5];
			corners[4] = item->matrix[0] + item->matrix[2] + item->matrix[4];
			corners[5] = item->matrix[1] + item->matrix[3] + item->matrix[5];
			corners[6] = item->matrix[2] + item->matrix[4];
			corners[7] = item->matrix[3] + item->matrix[5];
			memcpy(placed, object.quad, sizeof(placed));
			if (near_quad(corners, placed))
				matched = 1;
		}
		check(matched, "render: an image's corners are where the render draws it");
	}
	check(images == 2, "render: the page's two images were compared");
	pdf_page_editor_close(editor);
	pdf_display_list_destroy(list);
}

/* Pages 2, 3 and 4: the rotated corners, the stream not read, the object past q's limit. */
static void
test_others(
	struct pdf_document *document)
{
	static const double rotated[8] = { 30.0, 10.0, 30.0, 50.0, 10.0, 50.0, 10.0, 10.0 };
	struct pdf_page_editor *editor;
	struct pdf_edit_object object;
	unsigned status;
	int error;

	/* Page 2, /Rotate 90: user x, y is shown y, x. */
	error = pdf_page_editor_open(document, 1, &editor);
	check(error == 0 && pdf_page_editor_count(editor) == 1, "page 2: one image");
	if (error == 0) {
		memset(&object, 0, sizeof(object));
		object.size = sizeof(object);
		error = pdf_page_editor_object(editor, 0, &object);
		check(error == 0 && near_quad(object.quad, rotated), "page 2: the rotated image's corners 30,10 30,50 10,50 10,10");
		pdf_page_editor_close(editor);
	}

	/* Page 3: the second stream is not read, so the page is not editable (its first stream's image is still listed). */
	error = pdf_page_editor_open(document, 2, &editor);
	status = pdf_page_editor_status(editor);
	check(error == 0 && (status & PDF_EDIT_PAGE_SKIPPED) != 0U && (status & PDF_EDIT_PAGE_READ_ONLY) != 0U, "page 3: a stream not read makes the page read-only");
	pdf_page_editor_close(editor);

	/* Page 4: the image inside q nested 70 deep is left out. */
	error = pdf_page_editor_open(document, 3, &editor);
	status = pdf_page_editor_status(editor);
	check(error == 0 && pdf_page_editor_count(editor) == 1, "page 4: only the image before the deep q");
	check(error == 0 && (status & PDF_EDIT_PAGE_PARTIAL) != 0U, "page 4: the page says objects were left out");
	pdf_page_editor_close(editor);

	/* A page that is not there. */
	error = pdf_page_editor_open(document, 9, &editor);
	check(error != 0, "page 10: no such page");
}

/* The keys of page 1 are the same when the document is opened again. */
static void
test_again(
	const char *path,
	const struct pdf_edit_key keys[3])
{
	struct pdf_document *document;
	struct pdf_page_editor *editor;
	size_t index;
	size_t i;
	int error;

	/* The document again. */
	error = pdf_document_open(path, &document);
	if (error != 0) {
		check(0, "open again");
		return;
	}

	/* Each key finds its object. */
	error = pdf_page_editor_open(document, 0, &editor);
	for (i = 0; error == 0 && i < 3; i++) {
		error = pdf_page_editor_find(editor, &keys[i], &index);
		check(error == 0 && index == i, "again: the key finds the same object");
	}
	pdf_page_editor_close(editor);
	pdf_document_close(document);
}

/* Tells whether two sets of corners are the same within a hundredth of a point. */
static int
near_quad(
	const double quad[8],
	const double expected[8])
{
	size_t i;

	/* Each coordinate. */
	for (i = 0; i < 8; i++) {
		if (fabs(quad[i] - expected[i]) > 0.01)
			return 0;
	}

	/* The same. */
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
