/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pictures of the photos (ws157-p003): a file decoded -- a JPEG
 * (libjpeg-compat, turned as its EXIF orientation says), a PNG
 * (libpng-compat) or a GIF's first frame (libgif-compat), through the
 * decoding Image Viewer and Files share (userland/desktop/picture) -- and
 * made a size: a square thumbnail cut from its middle, or the whole
 * picture shrunk to fit a side for the window.  A picture is turned by
 * quarter turns as the user asked.  These run on the thumbnails' thread
 * (thumbs.c); they share nothing.
 */

#include "app.h"

#include "userland/desktop/picture/picture.h"

#include <compat/png/png.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The largest file read, and the largest picture decoded: a side, and its pixels in all. */
#define DECODE_FILE_MAX		(96UL * 1024UL * 1024UL)
#define DECODE_SIDE_MAX		16384U
#define DECODE_PIXELS_MAX	(64UL * 1024UL * 1024UL)

/* A GIF's bytes being read by libgif-compat: the bytes, their count and how far it has read. */
struct decode_gif_source {
	const unsigned char *data;
	size_t size;
	size_t at;
};

static int decode_read(const char *path, unsigned char **data, size_t *size);
static int decode_jpeg(const unsigned char *data, size_t size, struct kl_image *image);
static int decode_png(const unsigned char *data, size_t size, struct kl_image *image);
static int decode_gif(const unsigned char *data, size_t size, struct kl_image *image);
static int decode_gif_read(GifFileType *gif, GifByteType *bytes, int count);
static void decode_adopt(struct kl_picture *picture, struct kl_image *image);

/*
 * Decodes a photo's file.  Returns 0 (the image's pixels are the caller's,
 * kl_image_release), EINVAL for a file that is not a picture read, EFBIG,
 * ENOMEM, or an errno value of the file's.
 */
int
ph_decode(
	const char *path,
	struct kl_image *image)
{
	unsigned char *data;
	size_t size;
	int kind;
	int error;

	/* The file's bytes. */
	memset(image, 0, sizeof(*image));
	error = decode_read(path, &data, &size);
	if (error != 0)
		return error;

	/* Its kind's decoder. */
	kind = ph_picture_kind(data, size);
	switch (kind) {
	case PH_KIND_JPEG:
		error = decode_jpeg(data, size, image);
		break;
	case PH_KIND_PNG:
		error = decode_png(data, size, image);
		break;
	case PH_KIND_GIF:
		error = decode_gif(data, size, image);
		break;
	default:
		error = EINVAL;
		break;
	}

	/* The bytes go. */
	free(data);
	if (error == E2BIG)
		error = EFBIG;
	return error;
}

/*
 * Makes a square thumbnail of a side from the middle of a picture (the
 * whole of its shorter side).  Returns 0 or ENOMEM.
 */
int
ph_thumbnail(
	const struct kl_image *picture,
	int side,
	struct kl_image *thumb)
{
	struct kl_image middle;
	int error;
	int square;

	/* The square in its middle, the same pixels. */
	square = picture->width;
	if (picture->height < square)
		square = picture->height;
	middle.pixels = picture->pixels + (size_t)((picture->height - square) / 2) * picture->stride + (size_t)((picture->width - square) / 2);
	middle.width = square;
	middle.height = square;
	middle.stride = picture->stride;

	/* Scaled to the side. */
	error = kl_image_create(thumb, side, side);
	if (error != 0)
		return error;
	kl_image_scale(&middle, thumb);
	return 0;
}

/*
 * Makes a picture no larger than a side (the same when it fits), its
 * shape kept.  Returns 0 or ENOMEM; the picture given is the caller's.
 */
int
ph_fit(
	const struct kl_image *picture,
	int side,
	struct kl_image *fitted)
{
	double scale;
	double across;
	double down;
	int width;
	int height;
	int error;

	/* The size: the longer side made the side, when it is longer. */
	across = (double)side / (double)picture->width;
	down = (double)side / (double)picture->height;
	scale = across;
	if (down < scale)
		scale = down;
	if (scale > 1.0)
		scale = 1.0;
	width = (int)((double)picture->width * scale + 0.5);
	height = (int)((double)picture->height * scale + 0.5);
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	/* Scaled into it. */
	error = kl_image_create(fitted, width, height);
	if (error != 0)
		return error;
	kl_image_scale(picture, fitted);
	return 0;
}

/*
 * Makes a picture turned clockwise by quarter turns (0 to 3).  Returns 0
 * or ENOMEM; the picture given is the caller's.
 */
int
ph_turn(
	const struct kl_image *picture,
	int turns,
	struct kl_image *turned)
{
	const uint32_t *row;
	uint32_t *out;
	int width;
	int height;
	int x;
	int y;
	int error;

	/* The size: a quarter or three swap the sides. */
	turns &= 3;
	width = picture->width;
	height = picture->height;
	if (turns == 1 || turns == 3) {
		width = picture->height;
		height = picture->width;
	}

	/*  kl_image_create(turned=The turned picture. */
	error = kl_image_create(turned, width, height);
	if (error != 0)
		return error;

	/* Each source pixel to its place. */
	for (y = 0; y < picture->height; y++) {
		row = picture->pixels + (size_t)y * picture->stride;
		for (x = 0; x < picture->width; x++) {
			switch (turns) {
			case 1:
				out = turned->pixels + (size_t)x * turned->stride + (size_t)(picture->height - 1 - y);
				break;
			case 2:
				out = turned->pixels + (size_t)(picture->height - 1 - y) * turned->stride + (size_t)(picture->width - 1 - x);
				break;
			case 3:
				out = turned->pixels + (size_t)(picture->width - 1 - x) * turned->stride + (size_t)y;
				break;
			default:
				out = turned->pixels + (size_t)y * turned->stride + (size_t)x;
				break;
			}

			/*  row[x];=The pixel. */
			*out = row[x];
		}
	}

	/* Turned. */
	return 0;
}

/* Reads a whole file; 0 with its bytes (the caller's), EFBIG, ENOMEM, EIO, or an errno value. */
static int
decode_read(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	struct stat status;
	ssize_t got;
	size_t done;
	int regular;
	int error;
	int fd;

	/* A regular file of a size taken. */
	*data = NULL;
	*size = 0;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return errno;
	error = fstat(fd, &status);
	if (error != 0) {
		error = errno;
		(void)close(fd);
		return error;
	}

	/*  S_ISREG=A regular file. */
	regular = S_ISREG(status.st_mode);
	if (!regular || status.st_size <= 0) {
		(void)close(fd);
		return EINVAL;
	}

	/* Not too large. */
	if ((unsigned long)status.st_size > DECODE_FILE_MAX) {
		(void)close(fd);
		return EFBIG;
	}

	/* Its bytes. */
	*data = malloc((size_t)status.st_size);
	if (*data == NULL) {
		(void)close(fd);
		return ENOMEM;
	}

	/*  0;=Read to its end. */
	done = 0;
	while (done < (size_t)status.st_size) {
		got = read(fd, *data + done, (size_t)status.st_size - done);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			break;
		done += (size_t)got;
	}

	/* Read through. */
	(void)close(fd);
	if (done == 0U) {
		free(*data);
		*data = NULL;
		return EIO;
	}

	/*  done;=Succeeded. */
	*size = done;
	return 0;
}

/* Decodes a JPEG, turned as its EXIF orientation says; 0 or an errno value. */
static int
decode_jpeg(
	const unsigned char *data,
	size_t size,
	struct kl_image *image)
{
	struct kl_picture picture;
	int orientation;
	int error;

	/* Decoded within the sizes taken. */
	error = kl_picture_jpeg(NULL, data, size, DECODE_SIDE_MAX, DECODE_PIXELS_MAX, &picture, &orientation);
	if (error != 0)
		return error;

	/* Turned upright. */
	error = kl_picture_orient(&picture, orientation);
	if (error != 0) {
		free(picture.pixels);
		return error;
	}

	/* The image takes the pixels. */
	decode_adopt(&picture, image);
	return 0;
}

/* Decodes a PNG into premultiplied pixels; 0 or an errno value. */
static int
decode_png(
	const unsigned char *data,
	size_t size,
	struct kl_image *image)
{
	png_image png;
	unsigned char *pixels;
	const unsigned char *pixel;
	uint32_t *row;
	int x;
	int y;
	int ok;
	int error;

	/* The header: a size taken. */
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;
	ok = png_image_begin_read_from_memory(&png, data, size);
	if (!ok)
		return EINVAL;
	if (png.width == 0U || png.height == 0U || png.width > DECODE_SIDE_MAX || png.height > DECODE_SIDE_MAX ||
	    (unsigned long)png.width * png.height > DECODE_PIXELS_MAX) {
		png_image_free(&png);
		return EFBIG;
	}

	/* The pixels, 8-bit BGRA. */
	png.format = PNG_FORMAT_BGRA;
	pixels = malloc(PNG_IMAGE_SIZE(png));
	if (pixels == NULL) {
		png_image_free(&png);
		return ENOMEM;
	}

	/* Decoded. */
	ok = png_image_finish_read(&png, NULL, pixels, 0, NULL);
	if (!ok) {
		free(pixels);
		return EINVAL;
	}

	/* The image. */
	error = kl_image_create(image, (int)png.width, (int)png.height);
	if (error != 0) {
		free(pixels);
		return error;
	}

	/* Each pixel, its colour multiplied by its alpha. */
	for (y = 0; y < image->height; y++) {
		row = image->pixels + (size_t)y * image->stride;
		for (x = 0; x < image->width; x++) {
			pixel = pixels + ((size_t)y * png.width + (size_t)x) * 4U;
			row[x] = kl_picture_premultiply(pixel[2], pixel[1], pixel[0], pixel[3]);
		}
	}

	/* Succeeded: the decoded bytes go. */
	free(pixels);
	return 0;
}

/* Decodes a GIF's first frame; 0 or an errno value. */
static int
decode_gif(
	const unsigned char *data,
	size_t size,
	struct kl_image *image)
{
	struct decode_gif_source source;
	struct kl_picture picture;
	GifFileType *gif;
	int status;
	int error;

	/* The file's records, read from the bytes. */
	source.data = data;
	source.size = size;
	source.at = 0;
	error = 0;
	gif = DGifOpen(&source, decode_gif_read, &error);
	if (gif == NULL)
		return EINVAL;

	/* Every frame's record at once (the first is drawn). */
	status = DGifSlurp(gif);
	if (status != GIF_OK) {
		(void)DGifCloseFile(gif, &error);
		return EINVAL;
	}

	/* The first frame on its clear screen. */
	error = kl_picture_gif_first(gif, DECODE_SIDE_MAX, DECODE_PIXELS_MAX, &picture);
	(void)DGifCloseFile(gif, &status);
	if (error != 0)
		return error;

	/* The image takes the pixels. */
	decode_adopt(&picture, image);
	return 0;
}

/* Gives libgif-compat up to a count of the GIF's bytes; returns how many were given. */
static int
decode_gif_read(
	GifFileType *gif,
	GifByteType *bytes,
	int count)
{
	struct decode_gif_source *source;
	size_t left;

	/* The bytes not read yet, no more than asked for. */
	source = gif->UserData;
	left = source->size - source->at;
	if (count < 0)
		return 0;
	if ((size_t)count < left)
		left = (size_t)count;

	/* Copied out, and the place moves on. */
	memcpy(bytes, source->data + source->at, left);
	source->at += left;
	return (int)left;
}

/* Makes a decoded picture the image (the same pixels, freed by kl_image_release). */
static void
decode_adopt(
	struct kl_picture *picture,
	struct kl_image *image)
{
	/* The image takes the pixels; the picture no longer owns them. */
	image->pixels = picture->pixels;
	image->width = picture->width;
	image->height = picture->height;
	image->stride = (size_t)picture->width;
	picture->pixels = NULL;
}
