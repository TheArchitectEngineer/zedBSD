/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PDF of a Notes document (plan/ws079/design-pdf.md sections 1 to 3).
 *
 * Every page is written with libpdf's writer: each stroke's outline, the
 * polygon pdf_outline_stroke() makes and the screen fills too, as one
 * filled path in the stroke's colour and opacity.  The whole document goes
 * along as the edit data (encode.c), attached to the PDF.  The file is
 * written under a temporary name, put on the disk and renamed over the old
 * one, so a crash leaves either the old file or the new one.
 *
 * Opening a PDF needs libpdf's reader, which is not there yet: until it is,
 * notes_open_pdf() refuses, and recovery goes through the journal instead.
 */

#include "notes.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The longest path saved. */
#define SAVE_PATH_MAX		4096U

static int save_page(struct pdf_writer *writer, struct notes_page *page);
static int save_sync(const char *path);
static int save_sync_folder(const char *path);

/*
 * Saves a document as a PDF at a path, and tells how many bytes it has.
 *
 * On success the document is no longer dirty and keeps the PDF's
 * identifier for the next save.  Returns 0, or an errno value (the old
 * file is then unchanged).
 */
int
notes_save_pdf(
	struct notes_document *document,
	const char *path,
	size_t *bytes)
{
	char temporary[SAVE_PATH_MAX + 8U];
	struct notes_buffer edit;
	struct pdf_writer *writer;
	struct stat status;
	size_t index;
	int written;
	int result;
	int error;

	/* The temporary name beside the file. */
	written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	if (written < 0 || (size_t)written >= sizeof(temporary))
		return ENAMETOOLONG;

	/* The writer. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;

	/* The identifier a saved document keeps, and its creation date. */
	if (document->has_pdf_id) {
		error = pdf_writer_set_document_id(writer, document->pdf_id);
		if (error != 0) {
			pdf_writer_destroy(writer);
			return error;
		}
	}
	error = pdf_writer_set_dates(writer, (time_t)(document->time_base / 1000U), 0);
	if (error != 0) {
		pdf_writer_destroy(writer);
		return error;
	}

	/* Each page with its strokes. */
	for (index = 0; index < document->page_count; index++) {
		error = save_page(writer, document->pages[index]);
		if (error != 0) {
			pdf_writer_destroy(writer);
			return error;
		}
	}

	/* The edit data, attached. */
	notes_buffer_init(&edit);
	error = notes_encode_document(document, &edit);
	if (error == 0)
		error = pdf_writer_attach_file(writer, NOTES_ATTACHMENT_NAME, NOTES_ATTACHMENT_TYPE, edit.data, edit.length);
	notes_buffer_free(&edit);
	if (error != 0) {
		pdf_writer_destroy(writer);
		return error;
	}

	/* Writes the temporary file. */
	error = pdf_writer_save(writer, temporary);
	if (error != 0) {
		pdf_writer_destroy(writer);
		(void)unlink(temporary);
		return error;
	}

	/* Keeps the identifier the first save made. */
	result = pdf_writer_get_document_id(writer, document->pdf_id);
	if (result == 0)
		document->has_pdf_id = 1;
	pdf_writer_destroy(writer);

	/* Puts the bytes on the disk before the name. */
	error = save_sync(temporary);
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

	/* Replaces the old file in one step. */
	result = rename(temporary, path);
	if (result != 0) {
		error = errno;
		(void)unlink(temporary);
		return error;
	}

	/* Puts the new name on the disk. */
	(void)save_sync_folder(path);

	/* The file's size. */
	*bytes = 0;
	result = stat(path, &status);
	if (result == 0)
		*bytes = (size_t)status.st_size;

	/* Succeeded: the file is the document as it stands. */
	document->dirty = 0;
	return 0;
}

/*
 * Opens a PDF that Notes saved.
 *
 * libpdf cannot read PDFs yet (ws079-p004's reader), so this refuses with
 * ENOTSUP; once the reader is there it finds the attachment named
 * NOTES_ATTACHMENT_NAME and decodes it with notes_decode_document().
 */
int
notes_open_pdf(
	const char *path,
	struct notes_document *document)
{
	/* Nothing can be read yet. */
	(void)path;
	(void)document;

	/* Reports that reading is not supported. */
	return ENOTSUP;
}

/* Writes one page: each stroke's outline filled in its colour. */
static int
save_page(
	struct pdf_writer *writer,
	struct notes_page *page)
{
	struct notes_stroke *stroke;
	double red;
	double green;
	double blue;
	double alpha;
	size_t index;
	int error;

	/* The page. */
	error = pdf_writer_begin_page(writer, page->width, page->height);
	if (error != 0)
		return error;

	/* Each stroke, bottom first. */
	for (index = 0; index < page->stroke_count; index++) {
		/* The outline, the same polygon the screen draws. */
		stroke = page->strokes[index];
		error = notes_stroke_outline(stroke);
		if (error != 0)
			return error;

		/* The colour and its opacity. */
		red = (double)((stroke->color >> 24) & 0xffU) / 255.0;
		green = (double)((stroke->color >> 16) & 0xffU) / 255.0;
		blue = (double)((stroke->color >> 8) & 0xffU) / 255.0;
		alpha = (double)(stroke->color & 0xffU) / 255.0;
		error = pdf_writer_set_fill_color(writer, red, green, blue, alpha);
		if (error != 0)
			return error;

		/* The polygon, filled once. */
		error = pdf_writer_fill_outline(writer, stroke->outline, stroke->outline_count);
		if (error != 0)
			return error;
	}

	/* The page is finished. */
	error = pdf_writer_end_page(writer);
	if (error != 0)
		return error;

	/* Succeeded: the page is written. */
	return 0;
}

/* Puts a file's bytes on the disk. */
static int
save_sync(
	const char *path)
{
	int descriptor;
	int status;
	int error;

	/* Opens the file. */
	descriptor = open(path, O_RDONLY);
	if (descriptor < 0)
		return errno;

	/* Flushes it. */
	error = 0;
	status = fsync(descriptor);
	if (status != 0)
		error = errno;
	(void)close(descriptor);
	if (error != 0)
		return error;

	/* Succeeded: the bytes are on the disk. */
	return 0;
}

/* Puts the folder of a file on the disk, so that a new name survives a crash. */
static int
save_sync_folder(
	const char *path)
{
	char folder[SAVE_PATH_MAX];
	char *slash;
	size_t length;
	int error;

	/* The folder's path; a bare name is in the current folder. */
	length = strlen(path);
	if (length >= sizeof(folder))
		return ENAMETOOLONG;
	memcpy(folder, path, length + 1U);
	slash = strrchr(folder, '/');
	if (slash == NULL)
		return 0;
	if (slash == folder)
		slash[1] = '\0';
	else
		*slash = '\0';

	/* Flushes it. */
	error = save_sync(folder);
	if (error != 0)
		return error;

	/* Succeeded: the name is on the disk. */
	return 0;
}
