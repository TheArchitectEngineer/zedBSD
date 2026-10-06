/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p008: the host test of the image files Notes puts on a page
 * (picture-file.c):
 *
 *   host-notes-picture FOLDER
 *
 * FOLDER holds turned.jpg (8 by 4, EXIF orientation 6), cmyk.jpg,
 * rgb.png (7 by 5, 8-bit RGB), rgba.png (6 by 3, half transparent),
 * palette.png (5 by 4, a palette) and text.txt.  The JPEG keeps its bytes,
 * its size, components and orientation; the RGB PNG keeps its bytes; the
 * RGBA and palette PNGs become compressed RGBA (the RGBA one half
 * transparent); the CMYK JPEG and the text are refused (ENOTSUP).  Each
 * image inserted on a page of a new notebook is drawn by the page's
 * editor.  Prints each check and exits 0 when all passed.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "notes.h"

int notes_picture_load(struct notes_document *document, const char *path, struct notes_image **image);

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static int load(struct notes_document *document, const char *folder, const char *name, struct notes_image **image);
static int drawn(struct notes_document *document, struct notes_image *image);

int
main(
	int argc,
	char **argv)
{
	struct notes_document document;
	struct pdf_image_source source;
	struct notes_image *image;
	const unsigned char *pixels;
	void *owned;
	int error;

	/* A new notebook to put them in. */
	if (argc != 2)
		return 2;
	error = notes_document_init(&document, 0U);
	check(error == 0, "a new notebook");
	if (error != 0)
		return 1;

	/* The JPEG turned by its EXIF orientation: its bytes, size, components and orientation. */
	error = load(&document, argv[1], "turned.jpg", &image);
	check(error == 0 && image->kind == NOTES_IMAGE_JPEG && image->width == 8 && image->height == 4 && image->components == 3 && image->orientation == 6,
	      "turned.jpg: a JPEG of 8 by 4, 3 components, orientation 6");
	if (error == 0) {
		check(drawn(&document, image), "turned.jpg inserted and drawn");
		notes_image_release(image);
	}

	/* The RGB PNG keeps its bytes. */
	error = load(&document, argv[1], "rgb.png", &image);
	check(error == 0 && image->kind == NOTES_IMAGE_PNG && image->width == 7 && image->height == 5 && image->components == 3, "rgb.png: kept as it is, 7 by 5");
	if (error == 0) {
		check(drawn(&document, image), "rgb.png inserted and drawn");
		notes_image_release(image);
	}

	/* The RGBA PNG becomes compressed RGBA, half transparent. */
	error = load(&document, argv[1], "rgba.png", &image);
	check(error == 0 && image->kind == NOTES_IMAGE_RGBA && image->width == 6 && image->height == 3, "rgba.png: compressed RGBA, 6 by 3");
	if (error == 0) {
		error = notes_image_source(image, &source, &owned);
		pixels = source.data;
		check(error == 0 && source.kind == PDF_IMAGE_SOURCE_RGBA && source.bytes == 6U * 3U * 4U && pixels[3] > 100U && pixels[3] < 160U,
		      "rgba.png: its pixels half transparent");
		free(owned);
		check(drawn(&document, image), "rgba.png inserted and drawn");
		notes_image_release(image);
	}

	/* The palette PNG becomes RGBA too. */
	error = load(&document, argv[1], "palette.png", &image);
	check(error == 0 && image->kind == NOTES_IMAGE_RGBA && image->width == 5 && image->height == 4, "palette.png: compressed RGBA, 5 by 4");
	if (error == 0)
		notes_image_release(image);

	/* The refusals. */
	check(load(&document, argv[1], "cmyk.jpg", &image) == ENOTSUP, "cmyk.jpg: ENOTSUP");
	check(load(&document, argv[1], "text.txt", &image) == ENOTSUP, "text.txt: ENOTSUP");
	check(load(&document, argv[1], "missing.png", &image) == ENOENT, "a file that is not there: ENOENT");

	/* The notebook goes. */
	notes_document_free(&document);

	/* The summary. */
	printf("host-notes-picture: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Loads an image file of the folder. */
static int
load(
	struct notes_document *document,
	const char *folder,
	const char *name,
	struct notes_image **image)
{
	char path[1024];

	/* The path, then the file. */
	(void)snprintf(path, sizeof(path), "%s/%s", folder, name);
	return notes_picture_load(document, path, image);
}

/* Inserts an image on the first page and tells whether the page's editor draws it there (one more image). */
static int
drawn(
	struct notes_document *document,
	struct notes_image *image)
{
	struct pdf_page_editor *editor;
	struct pdf_display_list *list;
	struct notes_edit state;
	size_t images;
	size_t at;
	int error;

	/* The image at 10,10 to 50,30. */
	memset(&state, 0, sizeof(state));
	state.flags = NOTES_EDIT_INSERTED | NOTES_EDIT_IMAGE;
	state.id = document->next_id;
	document->next_id++;
	state.image = image;
	state.transform[0] = 40.0f;
	state.transform[3] = -20.0f;
	state.transform[4] = 10.0f;
	state.transform[5] = 30.0f;
	error = notes_document_edit_object(document, 0, &state);
	if (error != 0)
		return 0;

	/* The page drawn: as many images as edits. */
	error = notes_page_editor(document, 0, &editor);
	if (error == 0)
		error = pdf_page_editor_render(editor, (size_t)-1, &list);
	if (error != 0)
		return 0;
	images = 0;
	for (at = 0; at < list->count; at++) {
		if (list->items[at].type == PDF_ITEM_IMAGE)
			images++;
	}

	/* One a page's edit. */
	pdf_display_list_destroy(list);
	return images == document->pages[0]->edit_count;
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
