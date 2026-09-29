/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The images of Image Viewer (ws091): a PNG (libpng-compat), a JPEG
 * (libjpeg-compat, turned as its EXIF orientation says) or a GIF
 * (libgif-compat, every frame of an animated one composed) decoded into
 * premultiplied 0xAARRGGBB words.  The kind is told by the file's first
 * bytes, not by its name.
 *
 * An image larger than the viewer keeps (the presenter's largest texture,
 * IV_IMAGE_PIXELS_MAX pixels) is halved until it fits.  Transparent parts
 * are laid over a light checkerboard, so that every pixel the presenter
 * gets is opaque.  A still image then gets its levels of halves, each the
 * box average of the one before, down to a side of IV_LEVEL_SMALLEST, for
 * showing it small without shimmering.
 */

#include "imageview.h"

#include <compat/gif_lib.h>
#include <compat/jpeglib.h>
#include <compat/png/png.h>

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The smallest level side made (a level whose longer side is this or less is the last). */
#define IV_LEVEL_SMALLEST	256

/* The checkerboard under transparent parts: its square's side and its two colours (opaque 0xRRGGBB). */
#define IMAGE_CHECKER_SIDE	8
#define IMAGE_CHECKER_LIGHT	0xf2f4f7U
#define IMAGE_CHECKER_DARK	0xe3e7ecU

/* The shortest frame an animated GIF is shown for, and what a shorter delay is taken for, in milliseconds (as browsers do). */
#define IMAGE_DELAY_SHORTEST	20U
#define IMAGE_DELAY_DEFAULT	100U

/* The EXIF orientation tag, and the kinds of TIFF value it may come in. */
#define IMAGE_EXIF_ORIENTATION	0x0112U
#define IMAGE_TIFF_SHORT	3U

/* The JPEG APP1 marker, which carries EXIF. */
#define IMAGE_JPEG_APP1		(JPEG_APP0 + 1)

/*
 * A JPEG being read: libjpeg's error manager, which jumps back here
 * instead of ending the program, and where it jumps to.
 */
struct image_jpeg_error {
	struct jpeg_error_mgr manager;
	jmp_buf back;
};

/*
 * A decoded picture before it becomes an image: its premultiplied pixels,
 * its size and whether any pixel is not opaque.
 */
struct image_picture {
	uint32_t *pixels;
	int width;
	int height;
	int has_alpha;
};

static int image_kind(const char *path, const char **format);
static int image_png(struct iv_image *image, struct image_picture *picture);
static int image_jpeg(struct iv_image *image, struct image_picture *picture);
static void image_jpeg_exit(j_common_ptr info);
static void image_jpeg_quiet(j_common_ptr info, int level);
static int image_jpeg_orientation(j_decompress_ptr info);
static int image_gif(struct iv_image *image, struct image_picture *picture);
static int image_gif_frames(struct iv_image *image, GifFileType *gif, uint32_t *screen);
static void image_gif_draw(GifFileType *gif, int index, int transparent, uint32_t *screen);
static void image_gif_clear(GifFileType *gif, int index, uint32_t *screen);
static uint32_t image_premultiply(unsigned red, unsigned green, unsigned blue, unsigned alpha);
static int image_orient(struct image_picture *picture, int orientation);
static int image_halve(const uint32_t *pixels, int width, int height, uint32_t **result, int *result_width, int *result_height);
static int image_fit(struct iv_image *image, struct image_picture *picture, int max_dimension);
static void image_checker(uint32_t *pixels, int width, int height);
static int image_levels(struct iv_image *image);
static int image_refuse(struct iv_image *image, int error, const char *reason);
static unsigned image_read16(const unsigned char *data, int big_endian);
static unsigned long image_read32(const unsigned char *data, int big_endian);

/*
 * Decodes the image in a file, making it no larger than max_dimension on
 * either side (0: no limit on a side) and IV_IMAGE_PIXELS_MAX in all.
 *
 * Returns 0 with the image ready to show, or an errno value with
 * image->error and image->reason saying why (the image is then empty but
 * still has its path, and may be freed as usual).
 */
int
iv_image_load(
	struct iv_image *image,
	const char *path,
	int max_dimension)
{
	struct image_picture picture;
	uint64_t started;
	int kind;
	int error;

	/* Nothing decoded yet; the path is kept even when decoding fails.  The time it takes is logged. */
	started = iv_clock();
	memset(image, 0, sizeof(*image));
	snprintf(image->path, sizeof(image->path), "%s", path);
	memset(&picture, 0, sizeof(picture));

	/* The kind of image, by the file's first bytes. */
	kind = image_kind(path, &image->format);

	/* Decodes it by its kind. */
	switch (kind) {
	case 1:
		error = image_png(image, &picture);
		break;
	case 2:
		error = image_jpeg(image, &picture);
		break;
	case 3:
		error = image_gif(image, &picture);
		break;
	case -1:
		error = image_refuse(image, errno, "The file cannot be read");
		break;
	default:
		error = image_refuse(image, EINVAL, "This is not a PNG, JPEG or GIF image");
		break;
	}

	/* A picture that could not be decoded leaves the image empty with its reason. */
	if (error != 0) {
		free(picture.pixels);
		return error;
	}

	/* The picture as the image's level 0, halved when it is larger than the viewer keeps. */
	error = image_fit(image, &picture, max_dimension);
	if (error != 0) {
		free(picture.pixels);
		iv_image_free(image);
		(void)image_refuse(image, error, "There is not enough memory for this image");
		return error;
	}

	/* Transparent parts over the checkerboard, and the levels of a still image. */
	if (image->frame_count == 0U) {
		if (image->has_alpha)
			image_checker(image->levels[0].pixels, image->width, image->height);
		error = image_levels(image);
		if (error != 0) {
			iv_image_free(image);
			(void)image_refuse(image, error, "There is not enough memory for this image");
			return error;
		}
	}

	/* Succeeded: the image can be shown. */
	iv_log("IMAGE path=%s format=%s width=%d height=%d file=%dx%d levels=%lu frames=%lu ms=%llu", image->path, image->format, image->width,
	    image->height, image->file_width, image->file_height, (unsigned long)image->level_count, (unsigned long)image->frame_count,
	    (unsigned long long)(iv_clock() - started));
	return 0;
}

/*
 * Frees an image's pixels, levels and frames, and leaves it empty (its
 * path and its reason, if any, stay).
 */
void
iv_image_free(
	struct iv_image *image)
{
	size_t index;
	size_t first;

	/* An animated image's level 0 is its first frame, freed with the frames. */
	first = 0;
	if (image->frame_count != 0U)
		first = 1;

	/* The levels the image owns. */
	for (index = first; index < image->level_count; index++)
		free(image->levels[index].pixels);

	/* The frames and their delays. */
	for (index = 0; index < image->frame_count; index++)
		free(image->frames[index]);
	free(image->frames);
	free(image->delays);

	/* Nothing is left to show. */
	memset(image->levels, 0, sizeof(image->levels));
	image->level_count = 0;
	image->frames = NULL;
	image->delays = NULL;
	image->frame_count = 0;
	image->width = 0;
	image->height = 0;
}

/*
 * Reads the orientation (1 to 8) of an EXIF block (a JPEG's APP1 data,
 * from its "Exif" header); 1 (as stored) when it has none or cannot be
 * read.
 */
int
iv_image_orientation(
	const unsigned char *data,
	size_t size)
{
	const unsigned char *tiff;
	unsigned long directory;
	unsigned long offset;
	size_t tiff_size;
	unsigned count;
	unsigned index;
	unsigned tag;
	unsigned type;
	unsigned value;
	int big_endian;
	int match;

	/* The block starts "Exif" and two zeros, then the TIFF header. */
	if (size < 14U)
		return 1;
	match = memcmp(data, "Exif\0\0", 6U);
	if (match != 0)
		return 1;
	tiff = data + 6;
	tiff_size = size - 6U;

	/* The byte order: II (little-endian) or MM (big-endian), then 42. */
	if (tiff[0] == 'I' && tiff[1] == 'I') {
		big_endian = 0;
	} else if (tiff[0] == 'M' && tiff[1] == 'M') {
		big_endian = 1;
	} else {
		return 1;
	}

	/* The magic 42 confirms the header. */
	value = image_read16(tiff + 2, big_endian);
	if (value != 42U)
		return 1;

	/* The first directory, which must hold its count. */
	directory = image_read32(tiff + 4, big_endian);
	if (directory > tiff_size - 2U)
		return 1;
	count = image_read16(tiff + directory, big_endian);

	/* Looks through the directory's entries (twelve bytes each) for the orientation. */
	for (index = 0; index < count; index++) {
		offset = directory + 2U + (unsigned long)index * 12U;
		if (offset > tiff_size - 12U)
			return 1;

		/* Another tag is passed over. */
		tag = image_read16(tiff + offset, big_endian);
		if (tag != IMAGE_EXIF_ORIENTATION)
			continue;

		/* A SHORT value sits at the start of the entry's value field. */
		type = image_read16(tiff + offset + 2U, big_endian);
		if (type != IMAGE_TIFF_SHORT)
			return 1;
		value = image_read16(tiff + offset + 8U, big_endian);
		if (value < 1U || value > 8U)
			return 1;

		/* Reports the orientation the file gives. */
		return (int)value;
	}

	/* No orientation: the image is shown as stored. */
	return 1;
}

/*
 * Tells whether a file name is one the viewer shows (by its extension,
 * any case): 1 for .png, .jpg, .jpeg, .jpe and .gif, 0 otherwise.
 */
int
iv_image_is_name(
	const char *name)
{
	static const char *const extensions[] = { ".png", ".jpg", ".jpeg", ".jpe", ".gif" };
	const char *dot;
	size_t index;
	int match;

	/* The last dot starts the extension; a hidden file's leading dot does not. */
	dot = strrchr(name, '.');
	if (dot == NULL || dot == name)
		return 0;

	/* Compares it with each extension shown. */
	for (index = 0; index < sizeof(extensions) / sizeof(extensions[0]); index++) {
		match = strcasecmp(dot, extensions[index]);
		if (match == 0)
			return 1;
	}

	/* Another kind of file. */
	return 0;
}

/* Tells the kind of image by the file's first bytes: 1 PNG, 2 JPEG, 3 GIF, 0 another file, -1 an unreadable one. */
static int
image_kind(
	const char *path,
	const char **format)
{
	static const unsigned char png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	unsigned char head[8];
	size_t count;
	FILE *file;
	int match;

	/* The first eight bytes. */
	errno = 0;
	file = fopen(path, "rb");
	if (file == NULL)
		return -1;
	memset(head, 0, sizeof(head));
	count = fread(head, 1U, sizeof(head), file);
	fclose(file);
	*format = "unknown";

	/* PNG's signature. */
	match = memcmp(head, png, sizeof(png));
	if (count == sizeof(head) && match == 0) {
		*format = "PNG";
		return 1;
	}

	/* A JPEG starts with SOI and another marker. */
	if (count >= 3U && head[0] == 0xff && head[1] == 0xd8 && head[2] == 0xff) {
		*format = "JPEG";
		return 2;
	}

	/* A GIF starts GIF87a or GIF89a. */
	match = memcmp(head, "GIF8", 4U);
	if (count >= 6U && match == 0) {
		*format = "GIF";
		return 3;
	}

	/* Another kind of file. */
	return 0;
}

/* Decodes a PNG with libpng's simplified API into a picture. */
static int
image_png(
	struct iv_image *image,
	struct image_picture *picture)
{
	png_image png;
	const char *interlaced;
	unsigned char *bytes;
	size_t count;
	size_t index;
	int status;

	/* The header: the size and the kind. */
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;
	status = png_image_begin_read_from_file(&png, image->path);
	if (status == 0) {
		iv_log("IMAGE png refused path=%s message=%s", image->path, png.message);
		interlaced = strstr(png.message, "nterlace");
		if (interlaced != NULL)
			return image_refuse(image, ENOTSUP, "Interlaced PNG images are not supported");
		return image_refuse(image, EINVAL, "The PNG image is damaged");
	}

	/* A size the viewer can hold at all. */
	if (png.width == 0U || png.height == 0U || png.width > 32768U || png.height > 32768U) {
		png_image_free(&png);
		return image_refuse(image, E2BIG, "The image is too large");
	}

	/* Room for the pixels as 8-bit RGBA. */
	png.format = PNG_FORMAT_RGBA;
	count = (size_t)png.width * (size_t)png.height;
	bytes = malloc(count * 4U);
	if (bytes == NULL) {
		png_image_free(&png);
		return image_refuse(image, ENOMEM, "There is not enough memory for this image");
	}

	/* The pixels. */
	status = png_image_finish_read(&png, NULL, bytes, 0, NULL);
	if (status == 0) {
		iv_log("IMAGE png failed path=%s message=%s", image->path, png.message);
		free(bytes);
		png_image_free(&png);
		return image_refuse(image, EINVAL, "The PNG image is damaged");
	}

	/* The picture, premultiplied. */
	picture->pixels = malloc(count * sizeof(uint32_t));
	if (picture->pixels == NULL) {
		free(bytes);
		return image_refuse(image, ENOMEM, "There is not enough memory for this image");
	}

	/* Its size; opaque until a pixel says otherwise. */
	picture->width = (int)png.width;
	picture->height = (int)png.height;
	picture->has_alpha = 0;

	/* Each pixel's straight RGBA into a premultiplied word. */
	for (index = 0; index < count; index++) {
		picture->pixels[index] = image_premultiply(bytes[index * 4U], bytes[index * 4U + 1U], bytes[index * 4U + 2U], bytes[index * 4U + 3U]);
		if (bytes[index * 4U + 3U] != 255U)
			picture->has_alpha = 1;
	}

	/* The RGBA rows are no longer needed. */
	free(bytes);

	/* Succeeded: the picture as the file has it. */
	image->file_width = picture->width;
	image->file_height = picture->height;
	return 0;
}

/* Decodes a JPEG with libjpeg into a picture, turned as its EXIF orientation says. */
static int
image_jpeg(
	struct iv_image *image,
	struct image_picture *picture)
{
	struct jpeg_decompress_struct info;
	struct image_jpeg_error failure;
	unsigned char *volatile row;
	int orientation;
	JSAMPROW rows[1];
	unsigned char *line;
	unsigned cyan;
	unsigned magenta;
	unsigned yellow;
	unsigned black;
	unsigned x;
	int inverted;
	int cmyk;
	int error;
	FILE *file;

	/* The file. */
	file = fopen(image->path, "rb");
	if (file == NULL)
		return image_refuse(image, errno, "The file cannot be read");

	/* A decompressor whose errors come back here. */
	memset(&info, 0, sizeof(info));
	info.err = jpeg_std_error(&failure.manager);
	failure.manager.error_exit = image_jpeg_exit;
	failure.manager.emit_message = image_jpeg_quiet;
	row = NULL;

	/*
	 * An error in libjpeg comes back here (setjmp must stand in the
	 * condition itself); row is volatile so that it survives the jump.
	 */
	if (setjmp(failure.back) != 0) {
		jpeg_destroy_decompress(&info);
		fclose(file);
		free(row);
		free(picture->pixels);
		picture->pixels = NULL;
		return image_refuse(image, EINVAL, "The JPEG image is damaged or of a kind not supported");
	}

	/* The decompressor, reading the file. */
	jpeg_create_decompress(&info);
	jpeg_stdio_src(&info, file);

	/* The header, with APP1 (EXIF) kept for the orientation. */
	jpeg_save_markers(&info, IMAGE_JPEG_APP1, 0xffffU);
	(void)jpeg_read_header(&info, TRUE);
	orientation = image_jpeg_orientation(&info);

	/* RGB out, except CMYK, which is turned into RGB here. */
	cmyk = 0;
	if (info.jpeg_color_space == JCS_CMYK || info.jpeg_color_space == JCS_YCCK) {
		info.out_color_space = JCS_CMYK;
		cmyk = 1;
	} else {
		info.out_color_space = JCS_RGB;
	}

	/* The decoding starts; Adobe's marker says whether CMYK is stored inverted. */
	inverted = info.saw_Adobe_marker;
	(void)jpeg_start_decompress(&info);

	/* A size the viewer can hold at all. */
	if (info.output_width == 0U || info.output_height == 0U || info.output_width > 32768U || info.output_height > 32768U) {
		jpeg_destroy_decompress(&info);
		fclose(file);
		return image_refuse(image, E2BIG, "The image is too large");
	}

	/* The picture and a row of samples. */
	picture->width = (int)info.output_width;
	picture->height = (int)info.output_height;
	picture->has_alpha = 0;
	picture->pixels = malloc((size_t)picture->width * (size_t)picture->height * sizeof(uint32_t));
	row = malloc((size_t)picture->width * (size_t)info.output_components);
	if (picture->pixels == NULL || row == NULL) {
		jpeg_destroy_decompress(&info);
		fclose(file);
		free(row);
		return image_refuse(image, ENOMEM, "There is not enough memory for this image");
	}

	/* Each row, into opaque words. */
	line = row;
	while (info.output_scanline < info.output_height) {
		rows[0] = line;
		(void)jpeg_read_scanlines(&info, rows, 1U);

		/* Turns the row's samples into the picture's row. */
		for (x = 0; x < info.output_width; x++) {
			if (cmyk) {
				/* Adobe's CMYK is stored inverted; plain CMYK is not. */
				cyan = line[x * 4U];
				magenta = line[x * 4U + 1U];
				yellow = line[x * 4U + 2U];
				black = line[x * 4U + 3U];
				if (!inverted) {
					cyan = 255U - cyan;
					magenta = 255U - magenta;
					yellow = 255U - yellow;
					black = 255U - black;
				}

				/* An RGB or CMYK pixel, now in RGB, into the picture. */
				picture->pixels[(size_t)(info.output_scanline - 1U) * (size_t)picture->width + x] =
				    image_premultiply(cyan * black / 255U, magenta * black / 255U, yellow * black / 255U, 255U);
			} else {
				picture->pixels[(size_t)(info.output_scanline - 1U) * (size_t)picture->width + x] =
				    image_premultiply(line[x * 3U], line[x * 3U + 1U], line[x * 3U + 2U], 255U);
			}
		}
	}

	/* The decompressor and the file are done with. */
	(void)jpeg_finish_decompress(&info);
	jpeg_destroy_decompress(&info);
	fclose(file);
	free(row);

	/* The picture turned upright. */
	image->file_width = picture->width;
	image->file_height = picture->height;
	error = image_orient(picture, orientation);
	if (error != 0)
		return image_refuse(image, error, "There is not enough memory for this image");

	/* The file's size, turned the same way. */
	if (orientation >= 5) {
		image->file_width = picture->width;
		image->file_height = picture->height;
	}

	/* Succeeded: the picture upright. */
	return 0;
}

/* Ends a JPEG's decoding at an error: back to image_jpeg(). */
static void
image_jpeg_exit(
	j_common_ptr info)
{
	struct image_jpeg_error *failure;

	/* The error manager is the first member of image_jpeg_error. */
	failure = (struct image_jpeg_error *)(void *)info->err;
	longjmp(failure->back, 1);
}

/* Keeps libjpeg's warnings off the standard error (the viewer's log). */
static void
image_jpeg_quiet(
	j_common_ptr info,
	int level)
{
	/* A warning changes nothing the viewer shows. */
	(void)info;
	(void)level;
}

/* Reads a JPEG's orientation from its saved APP1 markers (1 when there is none). */
static int
image_jpeg_orientation(
	j_decompress_ptr info)
{
	jpeg_saved_marker_ptr marker;
	int orientation;

	/* The first APP1 that is EXIF gives it. */
	for (marker = info->marker_list; marker != NULL; marker = marker->next) {
		if (marker->marker != IMAGE_JPEG_APP1)
			continue;

		/* A readable orientation other than the default. */
		orientation = iv_image_orientation(marker->data, marker->data_length);
		if (orientation != 1)
			return orientation;
	}

	/* The image as stored. */
	return 1;
}

/* Decodes a GIF into a picture: its first frame, and every frame of an animated one into the image's frames. */
static int
image_gif(
	struct iv_image *image,
	struct image_picture *picture)
{
	GifFileType *gif;
	uint32_t *screen;
	size_t count;
	int status;
	int error;

	/* The file and its records. */
	error = 0;
	gif = DGifOpenFileName(image->path, &error);
	if (gif == NULL)
		return image_refuse(image, EINVAL, "The GIF image is damaged");
	status = DGifSlurp(gif);
	if (status != GIF_OK || gif->ImageCount < 1) {
		(void)DGifCloseFile(gif, &error);
		return image_refuse(image, EINVAL, "The GIF image is damaged");
	}

	/* A screen of a size the viewer can hold at all. */
	if (gif->SWidth <= 0 || gif->SHeight <= 0 || gif->SWidth > 32768 || gif->SHeight > 32768) {
		(void)DGifCloseFile(gif, &error);
		return image_refuse(image, E2BIG, "The image is too large");
	}

	/* The screen the frames are composed on, clear to start with. */
	count = (size_t)gif->SWidth * (size_t)gif->SHeight;
	screen = calloc(count, sizeof(uint32_t));
	if (screen == NULL) {
		(void)DGifCloseFile(gif, &error);
		return image_refuse(image, ENOMEM, "There is not enough memory for this image");
	}

	/* Every frame (an animated GIF), or the first frame only. */
	error = image_gif_frames(image, gif, screen);
	if (error != 0) {
		free(screen);
		(void)DGifCloseFile(gif, &error);
		return image_refuse(image, ENOMEM, "There is not enough memory for this image");
	}

	/* A still GIF's picture is the screen after its one frame; an animated one's is its first frame. */
	picture->width = gif->SWidth;
	picture->height = gif->SHeight;
	picture->has_alpha = 1;
	if (image->frame_count == 0U) {
		picture->pixels = screen;
	} else {
		free(screen);
		picture->pixels = NULL;
	}

	/* The file is done with. */
	(void)DGifCloseFile(gif, &error);

	/* Succeeded: the picture, or the frames. */
	image->file_width = picture->width;
	image->file_height = picture->height;
	return 0;
}

/*
 * Composes a GIF's frames on the screen.  With one frame, or frames too
 * large to keep, only the first is composed (on the screen); otherwise
 * each composed frame is kept, over the checkerboard, with its delay.
 */
static int
image_gif_frames(
	struct iv_image *image,
	GifFileType *gif,
	uint32_t *screen)
{
	GraphicsControlBlock control;
	uint32_t *saved;
	size_t count;
	size_t bytes;
	int index;
	int status;

	/* One frame, or too many to keep: the first frame only. */
	count = (size_t)gif->SWidth * (size_t)gif->SHeight;
	bytes = count * sizeof(uint32_t) * (size_t)gif->ImageCount;
	if (gif->ImageCount == 1 || bytes / (size_t)gif->ImageCount / sizeof(uint32_t) != count || bytes > IV_FRAMES_BYTES_MAX) {
		memset(&control, 0, sizeof(control));
		control.TransparentColor = NO_TRANSPARENT_COLOR;
		(void)DGifSavedExtensionToGCB(gif, 0, &control);
		image_gif_draw(gif, 0, control.TransparentColor, screen);

		/* Succeeded: one frame on the screen. */
		return 0;
	}

	/* The frames, their delays, and a copy of the screen for DISPOSE_PREVIOUS. */
	image->frames = calloc((size_t)gif->ImageCount, sizeof(image->frames[0]));
	if (image->frames == NULL)
		return ENOMEM;
	image->delays = calloc((size_t)gif->ImageCount, sizeof(image->delays[0]));
	if (image->delays == NULL)
		return ENOMEM;
	saved = malloc(count * sizeof(uint32_t));
	if (saved == NULL)
		return ENOMEM;

	/* Each frame: drawn over what the one before left, kept, then disposed of. */
	for (index = 0; index < gif->ImageCount; index++) {
		memset(&control, 0, sizeof(control));
		control.TransparentColor = NO_TRANSPARENT_COLOR;
		status = DGifSavedExtensionToGCB(gif, index, &control);
		if (status != GIF_OK) {
			control.DisposalMode = DISPOSAL_UNSPECIFIED;
			control.DelayTime = 0;
		}

		/* A frame to be undone keeps the screen before it. */
		if (control.DisposalMode == DISPOSE_PREVIOUS)
			memcpy(saved, screen, count * sizeof(uint32_t));
		image_gif_draw(gif, index, control.TransparentColor, screen);

		/* The composed frame over the checkerboard. */
		image->frames[index] = malloc(count * sizeof(uint32_t));
		if (image->frames[index] == NULL) {
			free(saved);
			return ENOMEM;
		}

		/* The composed frame, kept. */
		memcpy(image->frames[index], screen, count * sizeof(uint32_t));
		image_checker(image->frames[index], gif->SWidth, gif->SHeight);
		image->frame_count = (size_t)index + 1U;

		/* Its delay, in milliseconds, no shorter than browsers show one. */
		image->delays[index] = (unsigned)control.DelayTime * 10U;
		if (image->delays[index] < IMAGE_DELAY_SHORTEST)
			image->delays[index] = IMAGE_DELAY_DEFAULT;

		/* What the frame leaves for the next one. */
		if (control.DisposalMode == DISPOSE_BACKGROUND)
			image_gif_clear(gif, index, screen);
		else if (control.DisposalMode == DISPOSE_PREVIOUS)
			memcpy(screen, saved, count * sizeof(uint32_t));
	}

	/* The saved screen is no longer needed. */
	free(saved);

	/* Succeeded: every frame composed. */
	return 0;
}

/* Draws one frame of a GIF onto the screen, leaving its transparent colour's pixels as they are. */
static void
image_gif_draw(
	GifFileType *gif,
	int index,
	int transparent,
	uint32_t *screen)
{
	const SavedImage *frame;
	const ColorMapObject *map;
	const GifColorType *colour;
	unsigned value;
	int screen_x;
	int screen_y;
	int x;
	int y;

	/* The frame's colour map: its own, or the screen's. */
	frame = &gif->SavedImages[index];
	map = frame->ImageDesc.ColorMap;
	if (map == NULL)
		map = gif->SColorMap;
	if (map == NULL || frame->RasterBits == NULL)
		return;

	/* Each pixel of the frame that falls on the screen. */
	for (y = 0; y < frame->ImageDesc.Height; y++) {
		screen_y = frame->ImageDesc.Top + y;
		if (screen_y < 0 || screen_y >= gif->SHeight)
			continue;

		/* The row's pixels. */
		for (x = 0; x < frame->ImageDesc.Width; x++) {
			screen_x = frame->ImageDesc.Left + x;
			if (screen_x < 0 || screen_x >= gif->SWidth)
				continue;

			/* A transparent pixel, or a colour the map lacks, leaves the screen as it is. */
			value = frame->RasterBits[(size_t)y * (size_t)frame->ImageDesc.Width + (size_t)x];
			if ((int)value == transparent || (int)value >= map->ColorCount)
				continue;
			colour = &map->Colors[value];
			screen[(size_t)screen_y * (size_t)gif->SWidth + (size_t)screen_x] = image_premultiply(colour->Red, colour->Green, colour->Blue, 255U);
		}
	}
}

/* Clears a frame's rectangle of the screen to transparent (DISPOSE_BACKGROUND, as browsers treat it). */
static void
image_gif_clear(
	GifFileType *gif,
	int index,
	uint32_t *screen)
{
	const GifImageDesc *frame;
	int x;
	int y;

	/* Each pixel of the frame's rectangle on the screen. */
	frame = &gif->SavedImages[index].ImageDesc;
	for (y = frame->Top; y < frame->Top + frame->Height; y++) {
		if (y < 0 || y >= gif->SHeight)
			continue;

		/* The row, within the screen. */
		for (x = frame->Left; x < frame->Left + frame->Width; x++) {
			if (x >= 0 && x < gif->SWidth)
				screen[(size_t)y * (size_t)gif->SWidth + (size_t)x] = 0U;
		}
	}
}

/* Makes a premultiplied 0xAARRGGBB word of straight components. */
static uint32_t
image_premultiply(
	unsigned red,
	unsigned green,
	unsigned blue,
	unsigned alpha)
{
	uint32_t word;

	/* Each colour scaled by the alpha, rounded. */
	red = (red * alpha + 127U) / 255U;
	green = (green * alpha + 127U) / 255U;
	blue = (blue * alpha + 127U) / 255U;
	word = (uint32_t)alpha << 24;
	word |= (uint32_t)red << 16;
	word |= (uint32_t)green << 8;
	word |= (uint32_t)blue;

	/* Reports the word. */
	return word;
}

/*
 * Turns a picture as an EXIF orientation says (2 to 8; 1 leaves it):
 * mirrored, turned, or both, so that it shows upright.
 */
static int
image_orient(
	struct image_picture *picture,
	int orientation)
{
	uint32_t *turned;
	int width;
	int height;
	int source_x;
	int source_y;
	int x;
	int y;

	/* Upright already, or an orientation that is not one. */
	if (orientation <= 1 || orientation > 8)
		return 0;

	/* The turned picture's size: 5 to 8 swap the sides. */
	width = picture->width;
	height = picture->height;
	if (orientation >= 5) {
		width = picture->height;
		height = picture->width;
	}

	/* Room for the turned picture. */
	turned = malloc((size_t)width * (size_t)height * sizeof(uint32_t));
	if (turned == NULL)
		return ENOMEM;

	/* Each pixel of the turned picture, from where the orientation says it was. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			/* The source pixel of this orientation. */
			switch (orientation) {
			case 2:
				source_x = picture->width - 1 - x;
				source_y = y;
				break;
			case 3:
				source_x = picture->width - 1 - x;
				source_y = picture->height - 1 - y;
				break;
			case 4:
				source_x = x;
				source_y = picture->height - 1 - y;
				break;
			case 5:
				source_x = y;
				source_y = x;
				break;
			case 6:
				source_x = y;
				source_y = picture->height - 1 - x;
				break;
			case 7:
				source_x = picture->width - 1 - y;
				source_y = picture->height - 1 - x;
				break;
			default:
				source_x = picture->width - 1 - y;
				source_y = x;
				break;
			}

			/* The pixel into its turned place. */
			turned[(size_t)y * (size_t)width + (size_t)x] = picture->pixels[(size_t)source_y * (size_t)picture->width + (size_t)source_x];
		}
	}

	/* The turned picture replaces the stored one. */
	free(picture->pixels);
	picture->pixels = turned;
	picture->width = width;
	picture->height = height;

	/* Succeeded: the picture is upright. */
	return 0;
}

/* Makes a picture of half the size (rounded up), each pixel the average of the two by two it covers. */
static int
image_halve(
	const uint32_t *pixels,
	int width,
	int height,
	uint32_t **result,
	int *result_width,
	int *result_height)
{
	uint32_t *half;
	uint32_t sum[4];
	uint32_t pixel;
	int half_width;
	int half_height;
	int source_x;
	int source_y;
	int x;
	int y;
	int dx;
	int dy;
	int channel;

	/* The half size, at least one pixel. */
	half_width = (width + 1) / 2;
	half_height = (height + 1) / 2;
	half = malloc((size_t)half_width * (size_t)half_height * sizeof(uint32_t));
	if (half == NULL)
		return ENOMEM;

	/* Each pixel of the half, from the four it covers (an odd edge repeats its last row or column). */
	for (y = 0; y < half_height; y++) {
		for (x = 0; x < half_width; x++) {
			memset(sum, 0, sizeof(sum));

			/* The two by two, clamped to the picture. */
			for (dy = 0; dy < 2; dy++) {
				source_y = y * 2 + dy;
				if (source_y >= height)
					source_y = height - 1;

				/* The two of the row. */
				for (dx = 0; dx < 2; dx++) {
					source_x = x * 2 + dx;
					if (source_x >= width)
						source_x = width - 1;
					pixel = pixels[(size_t)source_y * (size_t)width + (size_t)source_x];

					/* Each channel of the pixel into its sum. */
					for (channel = 0; channel < 4; channel++)
						sum[channel] += (pixel >> (channel * 8)) & 0xffU;
				}
			}

			/* The average of each channel, rounded. */
			pixel = 0;
			for (channel = 0; channel < 4; channel++)
				pixel |= ((sum[channel] + 2U) / 4U) << (channel * 8);
			half[(size_t)y * (size_t)half_width + (size_t)x] = pixel;
		}
	}

	/* Succeeded: the half. */
	*result = half;
	*result_width = half_width;
	*result_height = half_height;
	return 0;
}

/*
 * Makes a decoded picture the image's level 0 (or its frames the image's
 * frames), halving it while it is larger than max_dimension on a side or
 * IV_IMAGE_PIXELS_MAX in all.
 */
static int
image_fit(
	struct iv_image *image,
	struct image_picture *picture,
	int max_dimension)
{
	uint32_t *half;
	size_t index;
	int width;
	int height;
	int too_large;
	int error;

	/* The picture as it came. */
	width = picture->width;
	height = picture->height;

	/* Halves it while it is too large (an animated image's frames each). */
	for (;;) {
		too_large = 0;
		if ((size_t)width * (size_t)height > IV_IMAGE_PIXELS_MAX)
			too_large = 1;
		if (max_dimension > 0 && (width > max_dimension || height > max_dimension))
			too_large = 1;
		if (!too_large || (width == 1 && height == 1))
			break;

		/* An animated image's frames. */
		if (image->frame_count != 0U) {
			for (index = 0; index < image->frame_count; index++) {
				error = image_halve(image->frames[index], width, height, &half, &picture->width, &picture->height);
				if (error != 0)
					return error;
				free(image->frames[index]);
				image->frames[index] = half;
			}
		} else {
			/* A still picture. */
			error = image_halve(picture->pixels, width, height, &half, &picture->width, &picture->height);
			if (error != 0)
				return error;
			free(picture->pixels);
			picture->pixels = half;
		}

		/* The next round halves the halved picture if it is still too large. */
		width = picture->width;
		height = picture->height;
		iv_log("IMAGE halved path=%s width=%d height=%d", image->path, width, height);
	}

	/* Level 0: the picture, or the first frame. */
	image->width = width;
	image->height = height;
	image->has_alpha = picture->has_alpha;
	image->levels[0].width = width;
	image->levels[0].height = height;
	image->level_count = 1;
	if (image->frame_count != 0U) {
		image->levels[0].pixels = image->frames[0];
	} else {
		image->levels[0].pixels = picture->pixels;
		picture->pixels = NULL;
	}

	/* Succeeded: level 0 is the image. */
	return 0;
}

/* Lays a picture's transparent parts over the checkerboard (premultiplied source over opaque squares). */
static void
image_checker(
	uint32_t *pixels,
	int width,
	int height)
{
	uint32_t square;
	uint32_t pixel;
	uint32_t alpha;
	uint32_t out;
	int channel;
	int x;
	int y;

	/* Each pixel that is not opaque. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
			alpha = pixel >> 24;
			if (alpha == 255U)
				continue;

			/* The square under it. */
			square = IMAGE_CHECKER_LIGHT;
			if (((x / IMAGE_CHECKER_SIDE) + (y / IMAGE_CHECKER_SIDE)) % 2 != 0)
				square = IMAGE_CHECKER_DARK;

			/* Each colour: the pixel's plus what it lets through of the square. */
			out = 0xff000000U;
			for (channel = 0; channel < 3; channel++) {
				out |= (((pixel >> (channel * 8)) & 0xffU) +
				    (((square >> (channel * 8)) & 0xffU) * (255U - alpha) + 127U) / 255U) << (channel * 8);
			}

			/* The pixel over its square, opaque. */
			pixels[(size_t)y * (size_t)width + (size_t)x] = out;
		}
	}
}

/* Makes a still image's levels of halves after level 0, down to IV_LEVEL_SMALLEST. */
static int
image_levels(
	struct iv_image *image)
{
	struct iv_level *level;
	struct iv_level *half;
	int error;

	/* Halves the last level while it is larger than the smallest kept. */
	while (image->level_count < IV_LEVELS_MAX) {
		level = &image->levels[image->level_count - 1U];
		if (level->width <= IV_LEVEL_SMALLEST && level->height <= IV_LEVEL_SMALLEST)
			break;

		/* The next level. */
		half = &image->levels[image->level_count];
		error = image_halve(level->pixels, level->width, level->height, &half->pixels, &half->width, &half->height);
		if (error != 0)
			return error;
		image->level_count++;
	}

	/* Succeeded: every level made. */
	return 0;
}

/* Leaves an image empty with an errno value and the reason in the viewer's words; returns the errno value. */
static int
image_refuse(
	struct iv_image *image,
	int error,
	const char *reason)
{
	/* A failure is never 0. */
	if (error == 0)
		error = EIO;

	/* The error and its reason. */
	image->error = error;
	snprintf(image->reason, sizeof(image->reason), "%s", reason);
	iv_log("IMAGE refused path=%s error=%d reason=%s", image->path, error, reason);

	/* Reports the error. */
	return error;
}

/* Reads a 16-bit number in a byte order. */
static unsigned
image_read16(
	const unsigned char *data,
	int big_endian)
{
	/* The most significant byte first, or last. */
	if (big_endian)
		return ((unsigned)data[0] << 8) | (unsigned)data[1];

	/* Reports the little-endian number. */
	return ((unsigned)data[1] << 8) | (unsigned)data[0];
}

/* Reads a 32-bit number in a byte order. */
static unsigned long
image_read32(
	const unsigned char *data,
	int big_endian)
{
	/* The most significant byte first, or last. */
	if (big_endian) {
		return ((unsigned long)data[0] << 24) | ((unsigned long)data[1] << 16) |
		    ((unsigned long)data[2] << 8) | (unsigned long)data[3];
	}

	/* Reports the little-endian number. */
	return ((unsigned long)data[3] << 24) | ((unsigned long)data[2] << 16) |
	    ((unsigned long)data[1] << 8) | (unsigned long)data[0];
}
