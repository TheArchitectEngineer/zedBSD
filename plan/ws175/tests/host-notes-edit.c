/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p007: the host test of Notes' model of the edits of a PDF's
 * objects, on make-edit-samples.py's edit-images.pdf, a JPEG and a PNG:
 *
 *   host-notes-edit FOLDER
 *
 * FOLDER holds edit-images.pdf, insert.jpg and insert.png, and gets the
 * files written (the journals under FOLDER/data).  Opened as another
 * program's PDF, page 1's image is moved, its form given the JPEG, its
 * inline image deleted and the PNG inserted; a new page gets the JPEG
 * inserted (a blank editor).  Undo, redo and Reset take them back and
 * make them again.  Saved, the edit data is version 2 and the file opens
 * again as Notes' (annotated) with the same edits and the images read back
 * from it; the journal recovers the same document; a notebook whose edit
 * names an object the page does not have opens as it is shown (rebased),
 * and its journal does not recover (set aside).  ws175-p004: a line of
 * edit-text.pdf given new words in its own font (and words it cannot
 * write refused by the page's editor), saved and opened again with them.
 * Prints each check and
 * exits 0 when all passed.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "notes.h"

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static unsigned char *read_file(const char *path, size_t *size);
static int copy_file(const char *from, const char *to);
static int same_edits(const struct notes_page *page, const struct notes_page *other);
static size_t count_images(struct notes_document *document, size_t page);
static int write_bogus(const char *base_path, const char *path);

int
main(
	int argc,
	char **argv)
{
	char path[1024];
	char saved_path[1024];
	char journal_path[1024];
	char kept_path[1100];
	char recovered_path[1024];
	struct notes_document document;
	struct notes_document opened;
	struct notes_document recovered;
	struct notes_image *jpeg_image;
	struct notes_image *png_image;
	struct notes_edit state;
	struct notes_edit inserted;
	struct pdf_document *file;
	struct notes_edit *bogus;
	struct stat status;
	const unsigned char *data;
	unsigned char *jpeg;
	unsigned char *png;
	const void *attachment;
	size_t attachment_size;
	size_t jpeg_size;
	size_t png_size;
	size_t records;
	size_t page;
	size_t index;
	size_t bytes;
	unsigned kind;
	int journal_there;
	int kept_there;
	int error;

	/* The files, and the journals' folder. */
	if (argc != 2)
		return 2;
	(void)snprintf(path, sizeof(path), "%s/insert.jpg", argv[1]);
	jpeg = read_file(path, &jpeg_size);
	(void)snprintf(path, sizeof(path), "%s/insert.png", argv[1]);
	png = read_file(path, &png_size);
	(void)snprintf(path, sizeof(path), "%s/data", argv[1]);
	(void)mkdir(path, 0700);
	setenv("XDG_DATA_HOME", path, 1);
	(void)snprintf(path, sizeof(path), "%s/edit-images.pdf", argv[1]);
	(void)snprintf(saved_path, sizeof(saved_path), "%s/notes-edit.pdf", argv[1]);
	error = copy_file(path, saved_path);
	check(error == 0 && jpeg != NULL && png != NULL, "the sample, the JPEG and the PNG");
	if (error != 0 || jpeg == NULL || png == NULL)
		return 1;

	/* Opened as another program's PDF, with a journal. */
	error = notes_open_pdf(saved_path, &document, &kind);
	check(error == 0 && kind == NOTES_OPENED_FOREIGN && document.page_count == 7, "opened as another program's PDF, 7 pages");
	if (error != 0)
		return 1;
	document.journal = notes_journal_create(saved_path);
	error = notes_journal_path(saved_path, journal_path, sizeof(journal_path));
	check(document.journal != NULL && error == 0, "its journal");

	/* The images. */
	jpeg_image = notes_image_create(&document, NOTES_IMAGE_JPEG, jpeg, jpeg_size, 8, 4, 3, 6);
	png_image = notes_image_create(&document, NOTES_IMAGE_PNG, png, png_size, 7, 5, 3, 0);
	check(jpeg_image != NULL && png_image != NULL && jpeg_image->id != png_image->id, "the JPEG (turned, orientation 6) and the PNG are images");

	/* Page 1: the image moved 10 to the right. */
	error = notes_page_object(&document, 0, 0, &state);
	state.flags = NOTES_EDIT_PLACED;
	state.transform[4] = 10.0f;
	if (error == 0)
		error = notes_document_edit_object(&document, 0, &state);
	check(error == 0 && document.pages[0]->edit_count == 1, "page 1: the image moved");

	/* The form given the JPEG, the inline image deleted. */
	error = notes_page_object(&document, 0, 1, &state);
	state.flags = NOTES_EDIT_IMAGE;
	state.image = jpeg_image;
	if (error == 0)
		error = notes_document_edit_object(&document, 0, &state);
	if (error == 0)
		error = notes_page_object(&document, 0, 2, &state);
	state.flags = NOTES_EDIT_DELETED;
	state.image = NULL;
	if (error == 0)
		error = notes_document_edit_object(&document, 0, &state);
	check(error == 0 && document.pages[0]->edit_count == 3, "the form given the JPEG, the inline image deleted");

	/* The PNG inserted at 20,10 to 55,35. */
	memset(&inserted, 0, sizeof(inserted));
	inserted.flags = NOTES_EDIT_INSERTED | NOTES_EDIT_IMAGE;
	inserted.id = document.next_id;
	document.next_id++;
	inserted.transform[0] = 35.0f;
	inserted.transform[3] = -25.0f;
	inserted.transform[4] = 20.0f;
	inserted.transform[5] = 35.0f;
	inserted.image = png_image;
	error = notes_document_edit_object(&document, 0, &inserted);
	check(error == 0 && document.pages[0]->edit_count == 4 && count_images(&document, 0) == 3, "the PNG inserted: the page draws 3 images (one deleted, one added)");

	/* A new page with the JPEG inserted (a blank editor). */
	error = notes_document_add_page(&document, 1);
	inserted.id = document.next_id;
	document.next_id++;
	inserted.image = jpeg_image;
	if (error == 0)
		error = notes_document_edit_object(&document, 1, &inserted);
	check(error == 0 && document.pages[1]->origin == NOTES_ORIGIN_NEW && count_images(&document, 1) == 1, "a new page with the JPEG inserted");

	/* A page's own object on a new page is refused. */
	state.flags = NOTES_EDIT_DELETED;
	check(notes_document_edit_object(&document, 1, &state) == EINVAL, "a page's own object on a new page: EINVAL");

	/* Undo takes the new page's image, redo puts it back. */
	error = notes_document_undo(&document, &page);
	check(error == 0 && page == 1 && document.pages[1]->edit_count == 0 && count_images(&document, 1) == 0, "undo: the new page's image gone");
	error = notes_document_redo(&document, &page);
	check(error == 0 && document.pages[1]->edit_count == 1 && count_images(&document, 1) == 1, "redo: it is back");

	/* Reset puts the moved image back; undo moves it again. */
	error = notes_page_object(&document, 0, 0, &state);
	if (error == 0)
		error = notes_document_reset_object(&document, 0, &state);
	check(error == 0 && document.pages[0]->edit_count == 3, "Reset: the image as the page has it");
	check(notes_document_reset_object(&document, 0, &state) == ENOENT, "Reset of an object without an edit: ENOENT");
	error = notes_document_undo(&document, &page);
	check(error == 0 && document.pages[0]->edit_count == 4 && (document.pages[0]->edits[0]->flags & NOTES_EDIT_PLACED) != 0U, "undo of Reset: moved again, in its place");

	/* The editor's index of an edit's object (ws175-p008: chosen again after an undo): the page's image 0, the PNG inserted 3. */
	error = notes_page_object_index(&document, 0, document.pages[0]->edits[0], &index);
	check(error == 0 && index == 0, "the moved image's index: 0");
	error = notes_page_object_index(&document, 0, document.pages[0]->edits[3], &index);
	check(error == 0 && index == 3, "the inserted PNG's index: 3");

	/* The images are held by the edits and the history, not by the test any more. */
	notes_image_release(jpeg_image);
	notes_image_release(png_image);

	/* Saved: the edit data is version 2. */
	error = notes_save_pdf(&document, saved_path, &bytes);
	check(error == 0, "saved");
	error = notes_journal_discard(document.journal);
	check(error == 0, "the journal discarded at the save");

	/* Opened again: Notes' own, the same edits, the images read back. */
	error = notes_open_pdf(saved_path, &opened, &kind);
	check(error == 0 && kind == NOTES_OPENED_ANNOTATED && opened.page_count == 8, "opened again: annotated, 8 pages");
	if (error == 0) {
		check(same_edits(document.pages[0], opened.pages[0]) && same_edits(document.pages[1], opened.pages[1]), "the same edits on pages 1 and 2");
		check(opened.pages[0]->edits[1]->image != NULL && opened.pages[0]->edits[1]->image->kind == NOTES_IMAGE_JPEG &&
		      opened.pages[0]->edits[1]->image->size == jpeg_size && memcmp(opened.pages[0]->edits[1]->image->data, jpeg, jpeg_size) == 0 &&
		      opened.pages[0]->edits[1]->image->orientation == 6, "the JPEG read back, its bytes and orientation");
		check(opened.pages[0]->edits[3]->image != NULL && opened.pages[0]->edits[3]->image->kind == NOTES_IMAGE_ROWS &&
		      opened.pages[0]->edits[3]->image->width == 7, "the PNG read back as its rows");
		check(opened.pages[1]->edits[0]->image == opened.pages[0]->edits[1]->image, "the JPEG of both pages is one image");
		check(count_images(&opened, 0) == 3 && count_images(&opened, 1) == 1, "the pages draw their images");

		/* The attached edit data is version 2. */
		check(opened.next_id >= document.next_id, "the numbers go on after the saved ones");
		notes_document_free(&opened);
	}

	/* The edit data in the file is version 2.0. */
	error = pdf_document_open(saved_path, &file);
	if (error == 0) {
		error = pdf_document_find_attachment_type(file, NOTES_ATTACHMENT_NAME, NOTES_ATTACHMENT_TYPE, &attachment, &attachment_size);
		data = attachment;
		check(error == 0 && attachment_size > 8U && data[4] == 2U && data[5] == 0U && data[6] == 0U, "the saved edit data is version 2.0");
		pdf_document_close(file);
	}

	/* A change logs the snapshot with its images; the journal recovers the same document. */
	error = notes_document_reset_object(&document, 1, document.pages[1]->edits[0]);
	check(error == 0 && document.pages[1]->edit_count == 0, "the new page's image taken off (logged)");
	error = notes_journal_recover(journal_path, &recovered, recovered_path, sizeof(recovered_path), &records);
	if (error == 0)
		error = notes_attach_base(recovered_path, &recovered);
	if (error == 0)
		error = notes_document_check_edits(&recovered);
	check(error == 0 && records >= 3, "the journal recovers (snapshot, images, the change)");
	if (error == 0) {
		check(same_edits(document.pages[0], recovered.pages[0]) && recovered.pages[1]->edit_count == 0, "the recovered edits are the document's");
		check(recovered.pages[0]->edits[1]->image != NULL && recovered.pages[0]->edits[1]->image->size == jpeg_size, "the recovered JPEG has its bytes");
		notes_document_free(&recovered);
	}

	/* An edit whose object is not on the page: logged, it does not recover (set aside). */
	memset(&state, 0, sizeof(state));
	state.key.kind = PDF_EDIT_IMAGE;
	state.key.offset = 1;
	state.key.length = 3;
	state.flags = NOTES_EDIT_DELETED;
	bogus = notes_edit_copy(&state);
	error = ENOMEM;
	if (bogus != NULL)
		error = notes_document_put_edit(&document, 0, document.pages[0]->edit_count, bogus);
	check(error == 0, "an edit of an object the page does not have, logged");
	error = notes_journal_recover(journal_path, &recovered, recovered_path, sizeof(recovered_path), &records);
	if (error == 0)
		error = notes_attach_base(recovered_path, &recovered);
	if (error == 0)
		error = notes_document_check_edits(&recovered);
	check(error == ESTALE, "its journal's edits do not apply: ESTALE");
	if (error == ESTALE)
		notes_document_free(&recovered);
	notes_journal_destroy(document.journal);
	document.journal = NULL;
	error = notes_journal_set_aside(journal_path);
	(void)snprintf(kept_path, sizeof(kept_path), "%s.kept", journal_path);
	journal_there = stat(journal_path, &status) == 0;
	kept_there = stat(kept_path, &status) == 0;
	check(error == 0 && !journal_there && kept_there, "the journal set aside as .kept");

	/* A notebook whose edit data names an object the page does not have: opened as it is shown. */
	(void)snprintf(path, sizeof(path), "%s/edit-images.pdf", argv[1]);
	(void)snprintf(recovered_path, sizeof(recovered_path), "%s/bogus.pdf", argv[1]);
	error = write_bogus(path, recovered_path);
	check(error == 0, "a notebook with an edit of an object the page does not have");
	error = notes_open_pdf(recovered_path, &opened, &kind);
	check(error == 0 && kind == NOTES_OPENED_REBASED && opened.pages[0]->edit_count == 0 && opened.pages[0]->origin == NOTES_ORIGIN_OVER, "it opens rebased: every page background, no edits");
	if (error == 0)
		notes_document_free(&opened);

	/* ws175-p004: a line of edit-text.pdf given new words, saved, opened again with them. */
	notes_document_free(&document);
	(void)snprintf(path, sizeof(path), "%s/edit-text.pdf", argv[1]);
	(void)snprintf(saved_path, sizeof(saved_path), "%s/notes-text.pdf", argv[1]);
	error = copy_file(path, saved_path);
	if (error == 0)
		error = notes_open_pdf(saved_path, &document, &kind);
	check(error == 0 && kind == NOTES_OPENED_FOREIGN, "edit-text.pdf opened as another program's PDF");
	if (error != 0)
		return 1;
	error = notes_page_object(&document, 0, 1, &state);
	state.flags = NOTES_EDIT_TEXT;
	state.text = "Changed!";
	if (error == 0)
		error = notes_document_edit_object(&document, 0, &state);
	check(error == 0 && count_images(&document, 0) == 0, "Line two given \"Changed!\" (the page's editor takes it)");
	state.text = "\xe6\x97\xa5";
	error = notes_document_edit_object(&document, 0, &state);
	check(error == 0 && count_images(&document, 0) == (size_t)-1, "Japanese the line's own font cannot write: the page's editor refuses it");
	error = notes_document_undo(&document, &page);
	check(error == 0 && count_images(&document, 0) == 0, "undo: \"Changed!\" again");
	error = notes_save_pdf(&document, saved_path, &bytes);
	check(error == 0, "the notebook with the new words saved");
	error = notes_open_pdf(saved_path, &opened, &kind);
	check(error == 0 && kind == NOTES_OPENED_ANNOTATED && opened.pages[0]->edit_count == 1 && (opened.pages[0]->edits[0]->flags & NOTES_EDIT_TEXT) != 0U &&
	      strcmp(opened.pages[0]->edits[0]->text, "Changed!") == 0, "opened again: the line's new words");
	if (error == 0)
		notes_document_free(&opened);

	/* What was made goes. */
	notes_document_free(&document);
	free(jpeg);
	free(png);

	/* The summary. */
	printf("host-notes-edit: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/*
 * Writes a notebook on a base PDF whose edit data has an edit of an object
 * page 1 does not have: the base, and a revision that keeps its pages with
 * that edit data.  Returns 0 or an errno value.
 */
static int
write_bogus(
	const char *base_path,
	const char *path)
{
	struct notes_document document;
	struct notes_buffer edit_data;
	struct notes_edit state;
	struct notes_edit *edit;
	struct pdf_writer *writer;
	unsigned kind;
	size_t index;
	int error;

	/* The base, as another program's PDF. */
	writer = NULL;
	error = notes_open_pdf(base_path, &document, &kind);
	if (error != 0)
		return error;

	/* The edit, put without its object being looked for. */
	memset(&state, 0, sizeof(state));
	state.key.kind = PDF_EDIT_IMAGE;
	state.key.offset = 1;
	state.key.length = 3;
	state.flags = NOTES_EDIT_DELETED;
	edit = notes_edit_copy(&state);
	error = ENOMEM;
	if (edit != NULL)
		error = notes_document_put_edit(&document, 0, 0, edit);
	if (error != 0) {
		notes_edit_free(edit);
		notes_document_free(&document);
		return error;
	}

	/* The revision: every page kept, the edit data attached. */
	notes_buffer_init(&edit_data);
	error = notes_encode_document(&document, &edit_data);
	if (error == 0)
		error = pdf_writer_create_update(document.base, &writer);
	for (index = 0; error == 0 && index < document.page_count; index++)
		error = pdf_writer_keep_page(writer, index);
	if (error == 0)
		error = pdf_writer_attach_file(writer, NOTES_ATTACHMENT_NAME, NOTES_ATTACHMENT_TYPE, edit_data.data, edit_data.length);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	if (writer != NULL)
		pdf_writer_destroy(writer);
	notes_buffer_free(&edit_data);
	notes_document_free(&document);
	return error;
}

/* Tells whether two pages have the same edits (flags, objects, transforms, images' numbers). */
static int
same_edits(
	const struct notes_page *page,
	const struct notes_page *other)
{
	const struct notes_edit *edit;
	const struct notes_edit *copy;
	size_t at;
	size_t item;
	int same;

	/* As many. */
	if (page->edit_count != other->edit_count)
		return 0;

	/* Each the same. */
	for (at = 0; at < page->edit_count; at++) {
		edit = page->edits[at];
		copy = other->edits[at];
		same = notes_edit_same_object(edit, copy);
		if (!same || edit->flags != copy->flags)
			return 0;
		for (item = 0; item < 6U; item++) {
			if (edit->transform[item] != copy->transform[item])
				return 0;
		}

		/* The image's number. */
		if ((edit->image == NULL) != (copy->image == NULL))
			return 0;
		if (edit->image != NULL && edit->image->id != copy->image->id)
			return 0;
	}

	/* All the same. */
	return 1;
}

/* Counts the images a page draws with its edits (its editor's preview). */
static size_t
count_images(
	struct notes_document *document,
	size_t page)
{
	struct pdf_page_editor *editor;
	struct pdf_display_list *list;
	size_t count;
	size_t at;
	int error;

	/* The page's editor and its preview. */
	error = notes_page_editor(document, page, &editor);
	if (error != 0)
		return (size_t)-1;
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	if (error != 0)
		return (size_t)-1;

	/* Its images. */
	count = 0;
	for (at = 0; at < list->count; at++) {
		if (list->items[at].type == PDF_ITEM_IMAGE)
			count++;
	}

	/* The count. */
	pdf_display_list_destroy(list);
	return count;
}

/* Copies a file.  Returns 0 or an errno value. */
static int
copy_file(
	const char *from,
	const char *to)
{
	unsigned char *data;
	size_t size;
	size_t written;
	FILE *file;

	/* The bytes. */
	data = read_file(from, &size);
	if (data == NULL)
		return ENOENT;

	/* Written. */
	file = fopen(to, "wb");
	if (file == NULL) {
		free(data);
		return EIO;
	}

	/* All the bytes. */
	written = fwrite(data, 1, size, file);
	fclose(file);
	free(data);
	if (written != size)
		return EIO;
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
