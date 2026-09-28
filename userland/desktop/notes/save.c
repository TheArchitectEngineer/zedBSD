/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PDF of a Notes document (plan/ws079/design-pdf.md sections 1 to 3).
 *
 * A notebook Notes made is written whole with libpdf's writer: each stroke's
 * outline, the polygon pdf_outline_stroke() makes and the screen fills too,
 * as one filled path in the stroke's colour and opacity, and the whole
 * document as the edit data (encode.c), attached.
 *
 * A notebook written on another program's PDF -- or on a notebook of which
 * another program changed pages -- keeps that PDF as its base.  Each save
 * writes the base's bytes unchanged and adds one revision (libpdf's
 * update): the strokes drawn over each page of the base that has them, the
 * pages Notes added, the edit data.  Since every save starts again from the
 * base, the revision of the last save is replaced rather than piled up, and
 * the base -- the other program's file -- stays as it was.
 *
 * Either file is written under a temporary name, put on the disk and
 * renamed over the old one, so a crash leaves the old file or the new one.
 *
 * Opening reads the edit data back and rebuilds the document from it; the
 * PDF's paths are not read.  A file whose newest revision is the one Notes
 * added to its base gets its base back and its strokes stay editable.  Any
 * other PDF becomes the base as it stands: its pages are drawn under the
 * strokes, except the pages of a notebook Notes saved that no other program
 * changed, whose strokes stay editable and are written anew in place.
 */

#include "notes.h"

#include <errno.h>
#include <fcntl.h>
#include <sha2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The longest path saved. */
#define SAVE_PATH_MAX		4096U

/* The largest PDF Notes opens, as libpdf's reader. */
#define SAVE_FILE_MAX		((size_t)512 * 1024 * 1024)

/* The size of each read while a file is loaded. */
#define SAVE_READ_CHUNK		65536U

static int save_whole(struct notes_document *document, const char *temporary);
static int save_update(struct notes_document *document, const char *temporary);
static int save_page(struct pdf_writer *writer, struct notes_page *page);
static int save_strokes(struct pdf_writer *writer, struct notes_page *page);
static int save_edit_data(struct pdf_writer *writer, struct notes_document *document);
static int save_sync(const char *path);
static int save_sync_folder(const char *path);
static int open_own(struct notes_document *saved, struct pdf_document *file);
static int open_base(struct notes_document *saved, const unsigned char *data, size_t size, struct pdf_document *file);
static int open_as_base(struct notes_document *document, struct notes_document *saved, int decoded, const unsigned char *data, size_t size, struct pdf_document *file, size_t *changed);
static int page_unchanged(const struct notes_page *page, struct pdf_document *file, size_t index);
static int read_file(const char *path, size_t limit, unsigned char **data, size_t *size);
static void hash_bytes(const unsigned char *data, size_t size, unsigned char digest[32]);

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
	struct stat status;
	int written;
	int result;
	int error;

	/* The temporary name beside the file. */
	written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	if (written < 0 || (size_t)written >= sizeof(temporary))
		return ENAMETOOLONG;

	/* Writes the temporary file: the whole notebook, or its base and a revision. */
	if (document->base != NULL) {
		error = save_update(document, temporary);
	} else {
		error = save_whole(document, temporary);
	}

	/* A file that could not be written goes. */
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

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
 * Opens a PDF into an empty document, and tells what it found (one of
 * NOTES_OPENED_*).
 *
 * A notebook Notes saved is rebuilt from its edit data.  Another
 * program's PDF becomes the document's base (design-pdf.md section 3):
 * each of its pages is drawn under the strokes, and the saves add to it.
 * Returns 0, EACCES for an encrypted PDF and EPERM for a signed one (Notes
 * does not write on them), the reader's errno value for a file it cannot
 * read (PDF_EFORMAT for a damaged one, ENOTSUP for one it does not read
 * yet), or ENOMEM.
 */
int
notes_open_pdf(
	const char *path,
	struct notes_document *document,
	unsigned *opened)
{
	struct notes_document saved;
	struct pdf_document *file;
	unsigned char *data;
	const void *edit;
	size_t edit_size;
	size_t changed;
	size_t size;
	int is_signed;
	int encrypted;
	int decoded;
	int error;

	/* The file's bytes. */
	error = read_file(path, SAVE_FILE_MAX, &data, &size);
	if (error != 0)
		return error;

	/* The PDF; one the reader refuses because it is encrypted is told apart. */
	error = pdf_document_open_memory(data, size, &file);
	if (error == ENOTSUP) {
		free(data);
		encrypted = 0;
		(void)pdf_document_encrypted(path, &encrypted);
		if (encrypted)
			return EACCES;
		return ENOTSUP;
	}

	/* Any other file the reader cannot read. */
	if (error != 0) {
		free(data);
		return error;
	}

	/* Refuses a signed PDF, whose signatures a new revision could invalidate. */
	error = pdf_document_signed(file, &is_signed);
	if (error == 0 && is_signed)
		error = EPERM;
	if (error != 0) {
		pdf_document_close(file);
		free(data);
		return error;
	}

	/* The edit data, when Notes wrote on the file before. */
	decoded = 0;
	memset(&saved, 0, sizeof(saved));
	error = pdf_document_find_attachment_type(file, NOTES_ATTACHMENT_NAME, NOTES_ATTACHMENT_TYPE, &edit, &edit_size);
	if (error == 0) {
		error = notes_decode_document(edit, edit_size, &saved);
		if (error == 0)
			decoded = 1;
	}

	/* A notebook written on another program's PDF, whose newest revision is Notes' own: its base again. */
	if (decoded && saved.base_size != 0U) {
		error = open_base(&saved, data, size, file);
		if (error == 0) {
			pdf_document_close(file);
			free(data);
			*document = saved;
			*opened = NOTES_OPENED_ANNOTATED;
			return 0;
		}
	}

	/* A notebook Notes saved, unchanged since. */
	if (decoded && saved.base_size == 0U) {
		error = open_own(&saved, file);
		if (error == 0) {
			pdf_document_close(file);
			free(data);
			*document = saved;
			*opened = NOTES_OPENED_NOTES;
			return 0;
		}
	}

	/* Any other PDF is the base as it stands. */
	changed = 0;
	error = open_as_base(document, &saved, decoded, data, size, file, &changed);
	if (decoded)
		notes_document_free(&saved);
	free(data);
	if (error != 0) {
		pdf_document_close(file);
		return error;
	}

	/* Succeeded: another program's PDF, or a notebook another program changed. */
	*opened = NOTES_OPENED_FOREIGN;
	if (decoded)
		*opened = NOTES_OPENED_CHANGED;
	return 0;
}

/*
 * Gives a document recovered from its journal the base its edit data
 * names: the first base_size bytes of the file, which must still hash as
 * they did.
 *
 * Returns 0 (also for a notebook without a base), ESTALE when the file no
 * longer starts with the base, or the errno value of reading it.
 */
int
notes_attach_base(
	const char *path,
	struct notes_document *document)
{
	struct pdf_document *base;
	unsigned char digest[32];
	unsigned char *data;
	size_t size;
	int differs;
	int error;

	/* A notebook written whole has no base. */
	if (document->base_size == 0U)
		return 0;

	/* The file, which must be at least as long as the base. */
	error = read_file(path, SAVE_FILE_MAX, &data, &size);
	if (error != 0)
		return error;
	if (size < document->base_size) {
		free(data);
		return ESTALE;
	}

	/* The base's bytes must be the ones the edit data recorded. */
	hash_bytes(data, (size_t)document->base_size, digest);
	differs = memcmp(digest, document->base_hash, sizeof(digest));
	if (differs != 0) {
		free(data);
		return ESTALE;
	}

	/* The base, read from those bytes. */
	error = pdf_document_open_memory(data, (size_t)document->base_size, &base);
	free(data);
	if (error != 0)
		return error;

	/* Succeeded: the document writes on its base again. */
	document->base = base;
	return 0;
}

/* Writes a notebook Notes made, whole, to a file. */
static int
save_whole(
	struct notes_document *document,
	const char *temporary)
{
	struct pdf_writer *writer;
	size_t index;
	int result;
	int error;

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

	/* The creation date is the document's; the modification date is the save's. */
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

	/* The edit data, with each page's content hash. */
	error = save_edit_data(writer, document);
	if (error != 0) {
		pdf_writer_destroy(writer);
		return error;
	}

	/* Writes the file. */
	error = pdf_writer_save(writer, temporary);
	if (error != 0) {
		pdf_writer_destroy(writer);
		return error;
	}

	/* Keeps the identifier the first save made. */
	result = pdf_writer_get_document_id(writer, document->pdf_id);
	if (result == 0)
		document->has_pdf_id = 1;
	pdf_writer_destroy(writer);

	/* Succeeded: the file holds the notebook. */
	return 0;
}

/*
 * Writes a notebook written on a base PDF: the base's bytes and a revision
 * with the strokes, to a file.
 *
 * Each page of the base is listed in order: kept when nothing is drawn on
 * it, drawn over, or -- a page whose content was Notes' own strokes --
 * drawn anew in place of its content.  The pages Notes added go between
 * them where they stand in the notebook.
 */
static int
save_update(
	struct notes_document *document,
	const char *temporary)
{
	struct pdf_writer *writer;
	struct notes_page *page;
	size_t index;
	int error;

	/* The writer that adds to the base; a signed base is refused. */
	error = pdf_writer_create_update(document->base, &writer);
	if (error != 0)
		return error;

	/* Each page, in the notebook's order. */
	for (index = 0; index < document->page_count; index++) {
		page = document->pages[index];

		/* A page of the base without strokes stays as the base has it. */
		if (page->origin == NOTES_ORIGIN_OVER && page->stroke_count == 0U) {
			error = pdf_writer_keep_page(writer, page->source);
			if (error != 0) {
				pdf_writer_destroy(writer);
				return error;
			}

			/* Nothing is drawn on it. */
			continue;
		}

		/* A page of the base with strokes: drawn over, or replaced when its content was Notes' own. */
		if (page->origin == NOTES_ORIGIN_OVER) {
			error = pdf_writer_begin_page_over(writer, page->source, PDF_PAGE_OVERLAY);
		} else if (page->origin == NOTES_ORIGIN_REPLACE) {
			error = pdf_writer_begin_page_over(writer, page->source, PDF_PAGE_REPLACE);
		} else {
			error = pdf_writer_begin_page(writer, page->width, page->height);
		}

		/* A page that could not be begun ends the save. */
		if (error != 0) {
			pdf_writer_destroy(writer);
			return error;
		}

		/* The page's strokes. */
		error = save_strokes(writer, page);
		if (error != 0) {
			pdf_writer_destroy(writer);
			return error;
		}

		/* The page is finished. */
		error = pdf_writer_end_page(writer);
		if (error != 0) {
			pdf_writer_destroy(writer);
			return error;
		}
	}

	/* The edit data, with the content hash of each page whose hash is known. */
	error = save_edit_data(writer, document);
	if (error != 0) {
		pdf_writer_destroy(writer);
		return error;
	}

	/* Writes the file: the base, then the revision. */
	error = pdf_writer_save(writer, temporary);
	pdf_writer_destroy(writer);
	if (error != 0)
		return error;

	/* Succeeded: the file holds the base and the notebook's revision. */
	return 0;
}

/* Writes one page of a notebook written whole: each stroke's outline filled in its colour. */
static int
save_page(
	struct pdf_writer *writer,
	struct notes_page *page)
{
	int error;

	/* The page. */
	error = pdf_writer_begin_page(writer, page->width, page->height);
	if (error != 0)
		return error;

	/* Its strokes. */
	error = save_strokes(writer, page);
	if (error != 0)
		return error;

	/* The page is finished. */
	error = pdf_writer_end_page(writer);
	if (error != 0)
		return error;

	/* Succeeded: the page is written. */
	return 0;
}

/* Writes a page's strokes, bottom first: each one's outline filled once in its colour. */
static int
save_strokes(
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

	/* Succeeded: every stroke is written. */
	return 0;
}

/*
 * Records each page's content hash in the document and attaches the edit
 * data to the writer.
 *
 * A page whose hash the writer does not know (a page of the base kept or
 * drawn over, whose content includes the base's own) records none.
 */
static int
save_edit_data(
	struct pdf_writer *writer,
	struct notes_document *document)
{
	struct notes_buffer edit;
	size_t index;
	int error;

	/* Each page's content hash, which the edit data records for the next opening to compare. */
	for (index = 0; index < document->page_count; index++) {
		error = pdf_writer_get_page_content_hash(writer, index, document->pages[index]->content_hash);
		if (error == ENOENT) {
			memset(document->pages[index]->content_hash, 0, sizeof(document->pages[index]->content_hash));
			error = 0;
		}

		/* Any other failure is the page's content's. */
		if (error != 0)
			return error;
	}

	/* The edit data, attached. */
	notes_buffer_init(&edit);
	error = notes_encode_document(document, &edit);
	if (error == 0)
		error = pdf_writer_attach_file(writer, NOTES_ATTACHMENT_NAME, NOTES_ATTACHMENT_TYPE, edit.data, edit.length);
	notes_buffer_free(&edit);
	if (error != 0)
		return error;

	/* Succeeded: the writer carries the edit data. */
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

/*
 * Checks a notebook Notes wrote whole against its file: the file has the
 * pages the edit data describes, and each page whose hash was recorded is
 * as Notes saved it.  On success the document takes the file's identifier.
 * Returns 0, or ESTALE for a file another program changed.
 */
static int
open_own(
	struct notes_document *saved,
	struct pdf_document *file)
{
	size_t pages;
	size_t index;
	int unchanged;
	int error;

	/* The file must have the pages the edit data describes. */
	pages = pdf_document_page_count(file);
	if (pages != saved->page_count)
		return ESTALE;

	/* Each page must be as Notes saved it. */
	for (index = 0; index < saved->page_count; index++) {
		unchanged = page_unchanged(saved->pages[index], file, index);
		if (!unchanged)
			return ESTALE;
	}

	/* The file's permanent identifier, which the next save keeps. */
	error = pdf_document_get_id(file, saved->pdf_id);
	if (error == 0)
		saved->has_pdf_id = 1;

	/* Succeeded: the notebook as it was saved, unchanged since. */
	return 0;
}

/*
 * Gives a notebook written on a base PDF its base back from the file: the
 * file must start with the base's bytes, and its newest revision must be
 * the only one added to them -- the one Notes saved.  Returns 0, or ESTALE
 * when another program changed the file since.
 */
static int
open_base(
	struct notes_document *saved,
	const unsigned char *data,
	size_t size,
	struct pdf_document *file)
{
	struct pdf_document *base;
	unsigned char digest[32];
	size_t base_newest;
	size_t base_previous;
	size_t file_newest;
	size_t file_previous;
	size_t file_pages;
	size_t base_pages;
	size_t index;
	int differs;
	int error;

	/* The file must start with the base's bytes. */
	if (size < saved->base_size)
		return ESTALE;
	hash_bytes(data, (size_t)saved->base_size, digest);
	differs = memcmp(digest, saved->base_hash, sizeof(digest));
	if (differs != 0)
		return ESTALE;

	/* The base, read from those bytes. */
	error = pdf_document_open_memory(data, (size_t)saved->base_size, &base);
	if (error != 0)
		return ESTALE;

	/* The file's newest revision must follow the base's directly. */
	(void)pdf_document_get_revision(base, &base_newest, &base_previous);
	(void)pdf_document_get_revision(file, &file_newest, &file_previous);
	if (file_previous != base_newest) {
		pdf_document_close(base);
		return ESTALE;
	}

	/* The file must have the notebook's pages, and each page of the base a page of it. */
	file_pages = pdf_document_page_count(file);
	if (file_pages != saved->page_count) {
		pdf_document_close(base);
		return ESTALE;
	}

	/* Each page of the base the notebook names must be one the base has. */
	base_pages = pdf_document_page_count(base);
	for (index = 0; index < saved->page_count; index++) {
		if (saved->pages[index]->origin == NOTES_ORIGIN_NEW)
			continue;
		if (saved->pages[index]->source >= base_pages) {
			pdf_document_close(base);
			return ESTALE;
		}
	}

	/* Succeeded: the notebook writes on its base again. */
	saved->base = base;
	saved->dirty = 0;
	return 0;
}

/*
 * Makes a document whose base is a PDF as it stands: each page of the file
 * a page of the base, drawn under the strokes -- except that a page of a
 * notebook Notes saved (the decoded edit data) that no other program
 * changed keeps its strokes, editable, to be written anew in place.  The
 * pages of saved that stay are taken from it.  changed counts the
 * notebook's pages that became background.
 */
static int
open_as_base(
	struct notes_document *document,
	struct notes_document *saved,
	int decoded,
	const unsigned char *data,
	size_t size,
	struct pdf_document *file,
	size_t *changed)
{
	struct notes_page **pages;
	struct notes_page *page;
	struct pdf_page_box box;
	size_t count;
	size_t index;
	int unchanged;
	int error;

	/* The pages; a file without any gets one blank page of Notes' own. */
	count = pdf_document_page_count(file);
	pages = calloc(count + 1U, sizeof(*pages));
	if (pages == NULL)
		return ENOMEM;

	/* Each page of the file, in order. */
	for (index = 0; index < count; index++) {
		/* A page of the notebook no other program changed keeps its strokes. */
		unchanged = 0;
		if (decoded && index < saved->page_count) {
			if (saved->pages[index]->origin != NOTES_ORIGIN_OVER)
				unchanged = page_unchanged(saved->pages[index], file, index);
		}

		/* Such a page is taken from the notebook, to be written anew in place. */
		if (unchanged) {
			page = saved->pages[index];
			saved->pages[index] = NULL;
			page->origin = NOTES_ORIGIN_REPLACE;
			page->source = index;
			pages[index] = page;
			continue;
		}

		/* Any other page is the base's, drawn under the strokes, at its size as shown. */
		page = NULL;
		error = pdf_document_page_box(file, index, &box);
		if (error == 0) {
			page = notes_page_create((float)box.width, (float)box.height, NOTES_BACKGROUND_PDF);
			if (page == NULL)
				error = ENOMEM;
		}

		/* A page that cannot be made ends the opening. */
		if (error != 0) {
			for (index = 0; index < count; index++)
				notes_page_free(pages[index]);
			free(pages);
			return error;
		}

		/* The page stands for the file's page; a notebook's page that became background is counted. */
		page->origin = NOTES_ORIGIN_OVER;
		page->source = index;
		pages[index] = page;
		if (decoded)
			(*changed)++;
	}

	/* A file without pages gets a blank one. */
	if (count == 0U) {
		pages[0] = notes_page_create(NOTES_PAGE_WIDTH, NOTES_PAGE_HEIGHT, NOTES_BACKGROUND_PLAIN);
		if (pages[0] == NULL) {
			free(pages);
			return ENOMEM;
		}

		/* The blank page is the notebook's only one. */
		count = 1U;
	}

	/* The document: the notebook's numbering and time when there was one. */
	memset(document, 0, sizeof(*document));
	document->pages = pages;
	document->page_count = count;
	document->page_capacity = count;
	document->next_id = 1U;
	document->time_base = (uint64_t)time(NULL) * 1000U;
	if (decoded) {
		document->next_id = saved->next_id;
		document->time_base = saved->time_base;
	}

	/* The file is the base, by its bytes' length and hash. */
	document->base = file;
	document->base_size = (uint64_t)size;
	hash_bytes(data, size, document->base_hash);

	/* Succeeded: the notebook writes on the file as it stands. */
	return 0;
}

/*
 * Tells whether a page of a notebook is as Notes saved it: its recorded
 * content hash matches the file's page.  A page without a recorded hash is
 * taken as it is; a page the reader cannot hash (for example compressed by
 * another program) has changed.
 */
static int
page_unchanged(
	const struct notes_page *page,
	struct pdf_document *file,
	size_t index)
{
	unsigned char digest[32];
	unsigned char zero[32];
	int differs;
	int error;

	/* A page whose hash was not recorded is taken as it is. */
	memset(zero, 0, sizeof(zero));
	differs = memcmp(page->content_hash, zero, sizeof(zero));
	if (differs == 0)
		return 1;

	/* The page's content in the file. */
	error = pdf_document_page_content_hash(file, index, digest);
	if (error != 0)
		return 0;

	/* Another program changed the page. */
	differs = memcmp(page->content_hash, digest, sizeof(digest));
	if (differs != 0)
		return 0;

	/* The page is as Notes saved it. */
	return 1;
}

/* Reads a whole file, of at most a limit of bytes, into a new allocation. */
static int
read_file(
	const char *path,
	size_t limit,
	unsigned char **data,
	size_t *size)
{
	unsigned char *buffer;
	unsigned char *grown;
	size_t length;
	size_t capacity;
	size_t got;
	FILE *stream;
	int failed;

	/* Opens the file. */
	stream = fopen(path, "rb");
	if (stream == NULL)
		return errno;

	/* Reads chunks until the end, growing the buffer ahead of each one. */
	buffer = NULL;
	length = 0;
	capacity = 0;
	for (;;) {
		/* Refuses a file past the limit. */
		if (length > limit) {
			free(buffer);
			fclose(stream);
			return EFBIG;
		}

		/* Makes room for the next chunk. */
		if (capacity - length < SAVE_READ_CHUNK) {
			capacity = capacity * 2U + SAVE_READ_CHUNK;
			grown = realloc(buffer, capacity);
			if (grown == NULL) {
				free(buffer);
				fclose(stream);
				return ENOMEM;
			}

			/* The bytes read so far are in the larger buffer. */
			buffer = grown;
		}

		/* Reads the chunk; a short one is the end or an error. */
		got = fread(buffer + length, 1U, SAVE_READ_CHUNK, stream);
		length += got;
		if (got < SAVE_READ_CHUNK)
			break;
	}

	/* Reports a read error rather than a short file. */
	failed = ferror(stream);
	fclose(stream);
	if (failed) {
		free(buffer);
		return EIO;
	}

	/* Succeeded: the caller owns the file's bytes. */
	*data = buffer;
	*size = length;
	return 0;
}

/* Computes the SHA-256 of bytes. */
static void
hash_bytes(
	const unsigned char *data,
	size_t size,
	unsigned char digest[32])
{
	SHA2_CTX context;

	/* The bytes in one pass. */
	SHA256Init(&context);
	SHA256Update(&context, data, size);
	SHA256Final(digest, &context);
}
