/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What a media file is (ws157-p013; preview.h --info): the media library's
 * mediastorage does not read the contents of the photos and videos it
 * keeps (the user's decision of 2026-10-11); it asks keiland-preview, in
 * its sandbox, for this.  The first PREVIEW_INFO_MAX bytes of the input
 * are read (from its start, whatever its offset), its kind told by its
 * first bytes (picture/media-kind.h), a picture's size read from its
 * header (PNG's IHDR, GIF's screen, JPEG's first frame header), and a
 * JPEG's date from its EXIF (photos/exif.c).  Nothing is decoded.
 *
 * The output is text, one field a line:
 *
 *   keiland-preview-info 1
 *   kind=jpeg|png|gif|video|none
 *   width=N
 *   height=N
 *   taken=N          (seconds of the camera's calendar as ph_time has them; empty when unknown)
 */

#include "preview.h"

#include "userland/desktop/photos/photos.h"
#include "userland/desktop/picture/media-kind.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The JPEG markers: a frame header's range (less DHT, JPG and DAC), APP1, start of scan, end of image. */
#define INFO_SOF_FIRST		0xc0U
#define INFO_SOF_LAST		0xcfU
#define INFO_DHT		0xc4U
#define INFO_JPG		0xc8U
#define INFO_DAC		0xccU
#define INFO_APP1		0xe1U
#define INFO_SOS		0xdaU
#define INFO_EOI		0xd9U

/* The longest output. */
#define INFO_TEXT_MAX		256U

static int info_read(int input, unsigned char **data, size_t *size);
static void info_png(const unsigned char *data, size_t size, long *width, long *height);
static void info_gif(const unsigned char *data, size_t size, long *width, long *height);
static void info_jpeg(const unsigned char *data, size_t size, long *width, long *height, int *dated, ph_time *taken);
static int info_write(int output, const char *text, size_t length);

/*
 * Tells what the input is, into the output.  Returns the exit status
 * (PREVIEW_OK even for a file of no kind: kind=none says so).
 */
int
preview_info(
	int input,
	int output)
{
	char text[INFO_TEXT_MAX];
	char date[32];
	unsigned char *data;
	const char *name;
	ph_time taken;
	size_t size;
	long width;
	long height;
	int dated;
	int kind;
	int length;
	int error;

	/* The input's start. */
	error = info_read(input, &data, &size);
	if (error == ENOMEM)
		return PREVIEW_NO_MEMORY;
	if (error != 0)
		return PREVIEW_DAMAGED;

	/* Its kind, and what its header says. */
	width = 0L;
	height = 0L;
	dated = 0;
	taken = 0;
	kind = kl_media_kind(data, size);
	switch (kind) {
	case KL_MEDIA_JPEG:
		name = "jpeg";
		info_jpeg(data, size, &width, &height, &dated, &taken);
		break;
	case KL_MEDIA_PNG:
		name = "png";
		info_png(data, size, &width, &height);
		break;
	case KL_MEDIA_GIF:
		name = "gif";
		info_gif(data, size, &width, &height);
		break;
	case KL_MEDIA_VIDEO:
		name = "video";
		break;
	default:
		name = "none";
		break;
	}

	/* The bytes are no longer needed. */
	free(data);

	/* The date, when there is one. */
	date[0] = '\0';
	if (dated)
		(void)snprintf(date, sizeof(date), "%lld", (long long)taken);

	/* The lines. */
	length = snprintf(text, sizeof(text), "keiland-preview-info 1\nkind=%s\nwidth=%ld\nheight=%ld\ntaken=%s\n", name, width, height, date);
	if (length < 0 || (size_t)length >= sizeof(text))
		return PREVIEW_NO_OUTPUT;
	error = info_write(output, text, (size_t)length);
	if (error != 0)
		return PREVIEW_NO_OUTPUT;

	/* Succeeded: told. */
	return PREVIEW_OK;
}

/* Reads up to PREVIEW_INFO_MAX bytes from the input's start; 0, ENOMEM or EIO. */
static int
info_read(
	int input,
	unsigned char **data,
	size_t *size)
{
	unsigned char *buffer;
	ssize_t got;
	size_t length;

	/* The room. */
	*data = NULL;
	*size = 0U;
	buffer = malloc(PREVIEW_INFO_MAX);
	if (buffer == NULL)
		return ENOMEM;

	/* Read from the start until the room is full or the file ends. */
	length = 0U;
	while (length < PREVIEW_INFO_MAX) {
		got = pread(input, buffer + length, PREVIEW_INFO_MAX - length, (off_t)length);
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0) {
			free(buffer);
			return EIO;
		}

		/* The end. */
		if (got == 0)
			break;
		length += (size_t)got;
	}

	/* Succeeded: the start of the file. */
	*data = buffer;
	*size = length;
	return 0;
}

/* Reads a PNG's size from its IHDR (the first chunk, at byte 8). */
static void
info_png(
	const unsigned char *data,
	size_t size,
	long *width,
	long *height)
{
	unsigned long across;
	unsigned long down;
	int same;

	/* The IHDR's fields, big-endian, where the format puts them. */
	if (size < 24U)
		return;
	same = memcmp(data + 12, "IHDR", 4U);
	if (same != 0)
		return;
	across = ((unsigned long)data[16] << 24) | ((unsigned long)data[17] << 16) | ((unsigned long)data[18] << 8) | data[19];
	down = ((unsigned long)data[20] << 24) | ((unsigned long)data[21] << 16) | ((unsigned long)data[22] << 8) | data[23];

	/* A sane size only (PNG's limit is 2^31 - 1). */
	if (across > 0x7fffffffUL || down > 0x7fffffffUL)
		return;
	*width = (long)across;
	*height = (long)down;
}

/* Reads a GIF's size from its logical screen (bytes 6 to 9, little-endian). */
static void
info_gif(
	const unsigned char *data,
	size_t size,
	long *width,
	long *height)
{
	/* The screen's width and height. */
	if (size < 10U)
		return;
	*width = (long)((unsigned)data[6] | ((unsigned)data[7] << 8));
	*height = (long)((unsigned)data[8] | ((unsigned)data[9] << 8));
}

/*
 * Walks a JPEG's marker segments up to the first scan: the first frame
 * header gives the size, an APP1 with EXIF the date.
 */
static void
info_jpeg(
	const unsigned char *data,
	size_t size,
	long *width,
	long *height,
	int *dated,
	ph_time *taken)
{
	size_t offset;
	size_t length;
	unsigned marker;
	int error;
	int frame;

	/* After the start of image, each segment until the scan or the end. */
	offset = 2U;
	frame = 0;
	while (offset + 4U <= size) {
		/* A marker, its fill bytes skipped. */
		if (data[offset] != 0xffU)
			return;
		marker = data[offset + 1U];
		if (marker == 0xffU) {
			offset++;
			continue;
		}

		/* The scan or the end: nothing more in the header. */
		if (marker == INFO_SOS || marker == INFO_EOI)
			return;

		/* Markers without a length. */
		if (marker == 0x01U || (marker >= 0xd0U && marker <= 0xd8U)) {
			offset += 2U;
			continue;
		}

		/* The segment's length, its two bytes included, within the data. */
		length = ((size_t)data[offset + 2U] << 8) | data[offset + 3U];
		if (length < 2U || offset + 2U + length > size)
			return;

		/* The first frame header: precision, height, width. */
		if (!frame &&
		    marker >= INFO_SOF_FIRST &&
		    marker <= INFO_SOF_LAST &&
		    marker != INFO_DHT &&
		    marker != INFO_JPG &&
		    marker != INFO_DAC &&
		    length >= 7U) {
			*height = (long)(((unsigned)data[offset + 5U] << 8) | data[offset + 6U]);
			*width = (long)(((unsigned)data[offset + 7U] << 8) | data[offset + 8U]);
			frame = 1;
		}

		/* An APP1 holding EXIF: the date (a block without a readable one is passed by). */
		if (marker == INFO_APP1 && !*dated) {
			error = ph_exif_date(data + offset + 4U, length - 2U, taken);
			if (error == 0)
				*dated = 1;
		}

		/* The next segment. */
		offset += 2U + length;
	}
}

/* Writes all the text with write(2) (no stdio on the output). */
static int
info_write(
	int output,
	const char *text,
	size_t length)
{
	ssize_t wrote;
	size_t done;

	/* Until it is all written. */
	done = 0U;
	while (done < length) {
		wrote = write(output, text + done, length - done);
		if (wrote < 0 && errno == EINTR)
			continue;
		if (wrote <= 0)
			return EIO;
		done += (size_t)wrote;
	}

	/* Succeeded: written. */
	return 0;
}
