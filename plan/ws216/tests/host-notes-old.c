/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p003: the Notes before the drawing tools, as a model for the host
 * test of the new one (run-notes-tools.sh builds this file with the Notes
 * sources of main d3a486f37, which read the edit data 1.1 and 2.0 and the
 * journal 2 only).
 *
 *   host-notes-old make DIR JPEG      writes DIR/old11.pdf and .bin (pens), DIR/old20.pdf and .bin (and an inserted
 *                                     JPEG), and leaves a journal 2 of DIR/journal.pdf in DIR/data (XDG_DATA_HOME)
 *   host-notes-old save BIN PDF       decodes edit data (or opens a notebook, BIN.pdf) and saves it as a PDF
 *   host-notes-old reencode BIN OUT   decodes edit data and encodes it again
 *   host-notes-old open PDF OUT       opens a PDF (prints "opened N"), adds a pen stroke on page 1, saves OUT
 */

#include "notes.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static struct notes_stroke *make_pen(struct notes_document *document, unsigned tool, float x0, float y0, float x1, float y1);
static int build(struct notes_document *document);
static int write_bin(const struct notes_document *document, const char *path);
static unsigned char *read_all(const char *path, size_t *size);
static int save_fixed(struct notes_document *document, const char *path);

int
main(
	int argc,
	char **argv)
{
	struct notes_document document;
	struct notes_image *image;
	struct notes_edit inserted;
	struct notes_stroke *stroke;
	unsigned char *data;
	char path[4096];
	size_t size;
	size_t bytes;
	unsigned opened;
	int error;

	/* The pens' notebook, the one with an inserted image, and a journal left behind. */
	if (argc == 4 && strcmp(argv[1], "make") == 0) {
		(void)snprintf(path, sizeof(path), "%s/data", argv[2]);
		(void)mkdir(path, 0700);
		if (path[0] != '/') {
			printf("FAIL: the data folder must be absolute (else Notes uses the user's own)\n");
			return 1;
		}
		setenv("XDG_DATA_HOME", path, 1);

		/* 1.1: pens and a highlighter on two pages, one cut by the eraser. */
		error = build(&document);
		(void)snprintf(path, sizeof(path), "%s/old11.pdf", argv[2]);
		if (error == 0)
			error = save_fixed(&document, path);
		(void)snprintf(path, sizeof(path), "%s/old11.bin", argv[2]);
		if (error == 0)
			error = write_bin(&document, path);
		if (error != 0) {
			printf("FAIL: old 1.1 (%d)\n", error);
			return 1;
		}

		/* 2.0: the same with a JPEG inserted on page 2. */
		data = read_all(argv[3], &size);
		image = NULL;
		if (data != NULL)
			image = notes_image_create(&document, NOTES_IMAGE_JPEG, data, size, 8, 4, 3, 1);
		free(data);
		memset(&inserted, 0, sizeof(inserted));
		inserted.flags = NOTES_EDIT_INSERTED | NOTES_EDIT_IMAGE;
		inserted.id = document.next_id;
		document.next_id++;
		inserted.transform[0] = 40.0f;
		inserted.transform[3] = -20.0f;
		inserted.transform[4] = 30.0f;
		inserted.transform[5] = 60.0f;
		inserted.image = image;
		error = EINVAL;
		if (image != NULL)
			error = notes_document_edit_object(&document, 1, &inserted);
		notes_image_release(image);
		(void)snprintf(path, sizeof(path), "%s/old20.pdf", argv[2]);
		if (error == 0)
			error = save_fixed(&document, path);
		(void)snprintf(path, sizeof(path), "%s/old20.bin", argv[2]);
		if (error == 0)
			error = write_bin(&document, path);
		notes_document_free(&document);
		if (error != 0) {
			printf("FAIL: old 2.0 (%d)\n", error);
			return 1;
		}

		/* A journal 2: the notebook changed and never saved. */
		error = build(&document);
		(void)snprintf(path, sizeof(path), "%s/journal.pdf", argv[2]);
		document.journal = notes_journal_create(path);
		if (error == 0 && document.journal == NULL)
			error = ENOMEM;
		if (error == 0) {
			stroke = make_pen(&document, NOTES_TOOL_PEN, 50.0f, 700.0f, 400.0f, 720.0f);
			error = notes_document_add_stroke(&document, 0, stroke);
		}
		if (error == 0)
			error = notes_document_add_page(&document, 2);
		(void)snprintf(path, sizeof(path), "%s/journal.bin", argv[2]);
		if (error == 0)
			error = write_bin(&document, path);
		notes_journal_destroy(document.journal);
		document.journal = NULL;
		notes_document_free(&document);
		if (error != 0) {
			printf("FAIL: old journal (%d)\n", error);
			return 1;
		}
		printf("host-notes-old: made\n");
		return 0;
	}

	/* Edit data (or a notebook, opened) saved as a PDF. */
	if (argc == 4 && strcmp(argv[1], "save") == 0) {
		size = strlen(argv[2]);
		if (size > 4U && strcmp(argv[2] + size - 4U, ".pdf") == 0) {
			error = notes_open_pdf(argv[2], &document, &opened);
		} else {
			data = read_all(argv[2], &size);
			error = EINVAL;
			if (data != NULL)
				error = notes_decode_document(data, size, &document);
			free(data);
		}
		if (error == 0) {
			error = save_fixed(&document, argv[3]);
			notes_document_free(&document);
		}
		if (error != 0) {
			printf("FAIL: old save (%d)\n", error);
			return 1;
		}
		return 0;
	}

	/* Edit data decoded and encoded again. */
	if (argc == 4 && strcmp(argv[1], "reencode") == 0) {
		data = read_all(argv[2], &size);
		error = EINVAL;
		if (data != NULL)
			error = notes_decode_document(data, size, &document);
		free(data);
		if (error == 0) {
			error = write_bin(&document, argv[3]);
			notes_document_free(&document);
		}
		if (error != 0) {
			printf("FAIL: old reencode (%d)\n", error);
			return 1;
		}
		return 0;
	}

	/* A PDF opened, written on with a pen and saved. */
	if (argc == 4 && strcmp(argv[1], "open") == 0) {
		error = notes_open_pdf(argv[2], &document, &opened);
		if (error != 0) {
			printf("FAIL: old open (%d)\n", error);
			return 1;
		}
		printf("opened %u\n", opened);
		stroke = make_pen(&document, NOTES_TOOL_PEN, 60.0f, 400.0f, 300.0f, 420.0f);
		error = notes_document_add_stroke(&document, 0, stroke);
		if (error == 0)
			error = notes_save_pdf(&document, argv[3], &bytes);
		notes_document_free(&document);
		if (error != 0) {
			printf("FAIL: old open and save (%d)\n", error);
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "usage: host-notes-old make DIR JPEG | save BIN PDF | reencode BIN OUT | open PDF OUT\n");
	return 2;
}

/* Makes a pen or highlighter stroke from one point to another as a wave, the pressure rising and falling. */
static struct notes_stroke *
make_pen(
	struct notes_document *document,
	unsigned tool,
	float x0,
	float y0,
	float x1,
	float y1)
{
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned index;
	float share;
	uint32_t id;

	/* The stroke, with the next number. */
	id = document->next_id;
	document->next_id++;
	stroke = notes_stroke_create(id, tool, 0x1f4e9cffU, 3.0f, document->time_base + 1000U * id);
	if (stroke == NULL)
		return NULL;

	/* Its samples. */
	for (index = 0; index <= 60U; index++) {
		share = (float)index / 60.0f;
		memset(&point, 0, sizeof(point));
		point.x = x0 + (x1 - x0) * share;
		point.y = y0 + (y1 - y0) * share + 15.0f * (float)sin(share * 9.0);
		point.pressure = (uint16_t)(65535.0 * sin(share * 3.14159));
		point.tilt_x = (int16_t)(index * 7U);
		point.tilt_y = -(int16_t)(index * 3U);
		point.time_ms = index * 9U;
		(void)notes_stroke_append(stroke, &point);
	}
	stroke->has_tilt = 1;
	return stroke;
}

/* Builds the pens' notebook: two pages, pens and a highlighter, a stroke cut by the eraser. */
static int
build(
	struct notes_document *document)
{
	struct notes_stroke *stroke;
	size_t cut;
	int error;

	/* The document, a fixed time base. */
	error = notes_document_init(document, 1760000000000ULL);
	if (error != 0)
		return error;
	error = notes_document_add_page(document, 1);

	/* Page 1: two pens and a highlighter. */
	if (error == 0) {
		stroke = make_pen(document, NOTES_TOOL_PEN, 50.0f, 100.0f, 500.0f, 140.0f);
		error = notes_document_add_stroke(document, 0, stroke);
	}
	if (error == 0) {
		stroke = make_pen(document, NOTES_TOOL_HIGHLIGHTER, 60.0f, 200.0f, 480.0f, 210.0f);
		error = notes_document_add_stroke(document, 0, stroke);
	}
	if (error == 0) {
		stroke = make_pen(document, NOTES_TOOL_PEN, 80.0f, 300.0f, 420.0f, 330.0f);
		error = notes_document_add_stroke(document, 0, stroke);
	}

	/* The last cut in two by the eraser. */
	if (error == 0) {
		notes_document_erase_begin(document);
		error = notes_document_erase_parts_at(document, 0, 250.0f, 315.0f, 12.0f, &cut);
		notes_document_erase_end(document);
	}

	/* Page 2: a pen. */
	if (error == 0) {
		stroke = make_pen(document, NOTES_TOOL_PEN, 100.0f, 500.0f, 300.0f, 520.0f);
		error = notes_document_add_stroke(document, 1, stroke);
	}
	return error;
}

/* Writes a document's edit data to a file. */
static int
write_bin(
	const struct notes_document *document,
	const char *path)
{
	struct notes_buffer buffer;
	FILE *file;
	int error;

	notes_buffer_init(&buffer);
	error = notes_encode_document(document, &buffer);
	if (error == 0) {
		file = fopen(path, "wb");
		if (file == NULL) {
			error = errno;
		} else {
			if (fwrite(buffer.data, 1U, buffer.length, file) != buffer.length)
				error = EIO;
			if (fclose(file) != 0)
				error = EIO;
		}
	}
	notes_buffer_free(&buffer);
	return error;
}

/* Saves a document under a fixed identifier, so that two saves of it can be compared. */
static int
save_fixed(
	struct notes_document *document,
	const char *path)
{
	size_t bytes;

	memset(document->pdf_id, 0x5a, sizeof(document->pdf_id));
	document->has_pdf_id = 1;
	return notes_save_pdf(document, path, &bytes);
}

/* Reads a whole file. */
static unsigned char *
read_all(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return NULL;
	}
	data = malloc((size_t)length + 1U);
	if (data != NULL && fread(data, 1U, (size_t)length, file) != (size_t)length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}
