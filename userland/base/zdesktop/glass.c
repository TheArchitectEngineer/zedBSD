/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The glass look of window mode (ws035-p059, a proof of the look and feel):
 * its images and the shapes it draws.  The windows, title bars and system
 * bar that use them are in shell.c.
 *
 * The wallpaper is a picture given with --wallpaper, or a pale blue
 * landscape the CPU draws once.  A quarter-size, blurred copy of it is the
 * frosted glass: a glass panel samples it at its own place on the output and
 * whitens it (like Windows' Mica, only the wallpaper shows through; windows
 * under a panel do not).
 *
 * Text is drawn from a glyph atlas made with libtruetype from the font at
 * server->font_path (printable ASCII and the multiplication sign); without a
 * font the look is drawn without text.  Any other character (WS070 p009) is
 * rendered when it is first drawn, from that font or else from the fallback
 * font (server->fallback_font_path, for Japanese), into a cell of the
 * atlas's cache, the least recently drawn cell making room; text is UTF-8.
 * The titlebar's icons (icons.c) are rendered into the atlas once, at two
 * sizes.
 */

#include "glass.h"

#include <truetype.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * The atlas holds printable ASCII, the multiplication sign (the close
 * button), the check mark and the right angle quote (the System Menu) at
 * five sizes.
 */
#define GLASS_GLYPHS		98U
#define GLASS_SIZES		5U
#define GLASS_ATLAS_WIDTH	1024U
#define GLASS_ATLAS_HEIGHT	1024U
#define GLASS_FILE_MAX		(16U * 1024U * 1024U)

/* The fonts kept open: the first font and the fallback. */
#define GLASS_FACES		2U

/* The icons' two sizes in pixels, and how many there are. */
#define GLASS_ICON_SIZES	2U

/* A cached glyph's cell in the atlas, in pixels a side, and the most cells there are. */
#define GLASS_CELL		48U
#define GLASS_CELLS		512U

/* The largest glyph rendered, in pixels a side. */
#define GLASS_BITMAP		64U

/* The blurred wallpaper is this many times smaller than the output. */
#define GLASS_BLUR_SCALE	4U
#define GLASS_BLUR_RADIUS	6U
#define GLASS_BLUR_PASSES	3U

/* One glyph's place in the atlas and its metrics in pixels. */
struct glass_glyph {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	int32_t left;
	int32_t top;
	int32_t advance;
};

/*
 * One cell of the atlas's cache: the character and size whose glyph it
 * holds (a codepoint of 0 is a free cell), the glyph's place and metrics,
 * and when it was last drawn.
 */
struct glass_cached {
	uint32_t codepoint;
	unsigned size;
	struct glass_glyph glyph;
	uint64_t used;
};

/*
 * The look's images and glyphs: the wallpaper and its blur, the atlas with
 * the ASCII glyphs at each size, the icons at their sizes, and the cache of
 * other characters; the fonts stay open (with their files' bytes) to
 * render the cache's glyphs.  cache_top is the atlas row the cache starts
 * at, cache_count the cells that fit under it, clock the count of cached
 * glyphs drawn (the cells' use is ordered by it).
 */
struct zwl_glass {
	struct zwl_import wallpaper;
	struct zwl_import blurred;
	struct zwl_import atlas;
	struct glass_glyph glyphs[GLASS_SIZES][GLASS_GLYPHS];
	struct glass_glyph icons[GLASS_ICON_SIZES][GLASS_ICON_COUNT];
	unsigned text;
	struct truetype_face *faces[GLASS_FACES];
	void *font_data[GLASS_FACES];
	unsigned face_count;
	const char *fallback_path;
	unsigned fallback_tried;
	struct glass_cached cache[GLASS_CELLS];
	uint32_t cache_top;
	unsigned cache_count;
	uint64_t clock;
};

static const unsigned glass_pixels[GLASS_SIZES] = { 14U, 15U, 20U, 36U, 24U };

/* The icons' sizes in pixels. */
static const unsigned glass_icon_pixels[GLASS_ICON_SIZES] = { 16U, 20U };

static int wallpaper_create(struct zwl_server *server, struct zwl_glass *glass);
static void wallpaper_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float *rgb);
static float ridge(float x, float base, float amplitude, float phase);
static int blur_create(struct zwl_server *server, struct zwl_glass *glass, const float *pixels);
static void blur_pass(float *pixels, float *scratch, uint32_t width, uint32_t height, int horizontal);
static uint32_t pack_pixel(const float *rgb);
static int atlas_create(struct zwl_server *server, struct zwl_glass *glass);
static int atlas_fill(struct zwl_glass *glass, struct truetype_face *face);
static int atlas_icons(struct zwl_glass *glass, uint32_t *pen_y);
static void atlas_put(struct zwl_glass *glass, const uint8_t *bitmap, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
static int glass_open_face(struct zwl_glass *glass, const char *path);
static const struct glass_glyph *glass_glyph_of(struct zwl_glass *glass, enum glass_size size, uint32_t codepoint);
static const struct glass_glyph *glass_cache_glyph(struct zwl_glass *glass, enum glass_size size, uint32_t codepoint);
static uint32_t glass_utf8_next(const char **text);
static void glass_draw_glyph_at(struct zwl_server *server, VkCommandBuffer command, const struct glass_glyph *glyph, int32_t x, int32_t baseline, const float *color);
static void *file_read(const char *path, size_t *size);
static float *wallpaper_load(const char *path, uint32_t width, uint32_t height);
static int ppm_number(const unsigned char *data, size_t size, size_t *at, uint32_t *number);

/*
 * Makes the wallpaper, its blurred copy and the glyph atlas.
 */
int
zwl_glass_open(
	struct zwl_server *server)
{
	struct zwl_glass *glass;
	int error;

	/* The look's state lives as long as window mode's device. */
	glass = calloc(1, sizeof(*glass));
	if (glass == NULL)
		return ENOMEM;
	server->compose->glass = glass;

	/* The wallpaper and the frosted glass made from it. */
	error = wallpaper_create(server, glass);
	if (error != 0)
		return error;

	/* The glyphs; the look is drawn without text when the font cannot be read. */
	error = atlas_create(server, glass);
	if (error != 0)
		printf("ZWL GLASS no text: font=%s errno=%d\n", server->font_path, error);

	/* Succeeded. */
	printf("ZWL GLASS ready text=%u\n", glass->text);
	return 0;
}

/*
 * Releases the look's images.
 */
void
zwl_glass_close(
	struct zwl_server *server)
{
	struct zwl_glass *glass;
	unsigned face;

	/* Nothing was made. */
	if (server->compose == NULL || server->compose->glass == NULL)
		return;

	/* The fonts kept open for the cache. */
	glass = server->compose->glass;
	for (face = 0; face < glass->face_count; face++) {
		truetype_close(glass->faces[face]);
		free(glass->font_data[face]);
	}

	/* The images, then the record. */
	zwl_host_image_release(server->compose, &glass->wallpaper);
	zwl_host_image_release(server->compose, &glass->blurred);
	zwl_host_image_release(server->compose, &glass->atlas);
	free(glass);
	server->compose->glass = NULL;
}

/* Draws the wallpaper once, and its blurred copy. */
static int
wallpaper_create(
	struct zwl_server *server,
	struct zwl_glass *glass)
{
	uint32_t *row;
	float *pixels;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
	VkResult result;
	int error;

	/* The output-sized image. */
	width = server->width;
	height = server->height;
	result = zwl_host_image_create(server->compose, width, height, server->compose->sampler, &glass->wallpaper);
	if (result != VK_SUCCESS)
		return EIO;

	/* The picture given, in floating point and kept for the blur. */
	pixels = NULL;
	if (server->wallpaper_path != NULL) {
		pixels = wallpaper_load(server->wallpaper_path, width, height);
		if (pixels == NULL)
			printf("ZWL GLASS no wallpaper: path=%s errno=%d\n", server->wallpaper_path, errno);
	}

	/* Otherwise the landscape drawn here. */
	if (pixels == NULL) {
		pixels = malloc((size_t)width * height * 3U * sizeof(float));
		if (pixels == NULL)
			return ENOMEM;
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++)
				wallpaper_pixel(x, y, width, height, &pixels[((size_t)y * width + x) * 3U]);
		}
	}

	/* Into the image. */
	for (y = 0; y < height; y++) {
		row = (uint32_t *)((unsigned char *)glass->wallpaper.map + y * glass->wallpaper.row_pitch);
		for (x = 0; x < width; x++)
			row[x] = pack_pixel(&pixels[((size_t)y * width + x) * 3U]);
	}

	/* The frosted glass. */
	error = blur_create(server, glass, pixels);
	free(pixels);
	return error;
}

/*
 * Colors one pixel of the landscape: a sky that pales towards the horizon
 * with a soft sun, three ranges of misty mountains, and a still lake that
 * reflects them.
 */
static void
wallpaper_pixel(
	uint32_t x,
	uint32_t y,
	uint32_t width,
	uint32_t height,
	float *rgb)
{
	static const float ranges[3][5] = {
		/* base, amplitude, phase, and the color's green and blue (red is below). */
		{ 0.50f, 0.10f, 0.3f, 0.82f, 0.94f },
		{ 0.58f, 0.09f, 2.1f, 0.74f, 0.90f },
		{ 0.66f, 0.07f, 4.7f, 0.64f, 0.85f }
	};
	static const float reds[3] = { 0.74f, 0.62f, 0.50f };
	float u;
	float v;
	float mirror;
	float top;
	float mist;
	float sun;
	float lake;
	unsigned index;

	/* The place, as fractions of the output. */
	u = (float)x / (float)width;
	v = (float)y / (float)height;
	lake = 0.80f;

	/* Under the lake's edge the landscape is seen in the water. */
	mirror = v;
	if (v > lake)
		mirror = 2.0f * lake - v;

	/* The sky: pale blue above, nearly white at the horizon, a glow to the upper right. */
	rgb[0] = 0.78f + 0.18f * mirror;
	rgb[1] = 0.86f + 0.11f * mirror;
	rgb[2] = 0.97f + 0.02f * mirror;
	sun = expf(-((u - 0.78f) * (u - 0.78f) * 18.0f + (mirror - 0.18f) * (mirror - 0.18f) * 30.0f));
	rgb[0] += 0.12f * sun;
	rgb[1] += 0.09f * sun;
	rgb[2] += 0.03f * sun;

	/* Each range in front of the ones behind it, misty towards its foot. */
	for (index = 0; index < 3U; index++) {
		top = ridge(u, ranges[index][0], ranges[index][1], ranges[index][2]);
		if (mirror < top)
			continue;
		mist = (mirror - top) / 0.18f;
		if (mist > 1.0f)
			mist = 1.0f;
		rgb[0] = reds[index] + (0.92f - reds[index]) * mist * 0.55f;
		rgb[1] = ranges[index][3] + (0.95f - ranges[index][3]) * mist * 0.55f;
		rgb[2] = ranges[index][4] + (0.99f - ranges[index][4]) * mist * 0.55f;
	}

	/* The water is darker and bluer than what it reflects, deeper towards the bottom. */
	if (v > lake) {
		rgb[0] = rgb[0] * 0.88f - (v - lake) * 0.35f;
		rgb[1] = rgb[1] * 0.92f - (v - lake) * 0.25f;
		rgb[2] = rgb[2] * 0.98f - (v - lake) * 0.08f;
		rgb[0] += 0.006f * sinf((float)y * 0.9f + u * 3.0f);
		rgb[1] += 0.006f * sinf((float)y * 0.9f + u * 3.0f);
	}

	/* Each channel within 0..1. */
	for (index = 0; index < 3U; index++) {
		if (rgb[index] > 1.0f)
			rgb[index] = 1.0f;
		if (rgb[index] < 0.0f)
			rgb[index] = 0.0f;
	}
}

/* The height (as a fraction of the output) of a mountain range's ridge at x. */
static float
ridge(
	float x,
	float base,
	float amplitude,
	float phase)
{
	float wave;
	float peak;

	/* A few waves of falling length make saddles, and a sharp term the peaks. */
	wave = 0.55f * sinf(x * 5.1f + phase);
	wave += 0.30f * sinf(x * 11.3f + phase * 1.7f);
	wave += 0.15f * sinf(x * 23.9f + phase * 2.3f);
	peak = 1.0f - fabsf(sinf(x * 4.3f + phase * 0.7f));
	wave += 0.60f * peak * peak * peak;

	/* Higher waves are lower on the output. */
	return base - amplitude * wave;
}

/*
 * Makes the frosted glass: the wallpaper averaged down by GLASS_BLUR_SCALE
 * and blurred by repeated box passes (close to a Gaussian).
 */
static int
blur_create(
	struct zwl_server *server,
	struct zwl_glass *glass,
	const float *pixels)
{
	uint32_t *row;
	float *small;
	float *scratch;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
	uint32_t dx;
	uint32_t dy;
	uint32_t channel;
	unsigned pass;
	size_t at;
	VkResult result;

	/* The small image, sampled linearly. */
	width = server->width / GLASS_BLUR_SCALE;
	height = server->height / GLASS_BLUR_SCALE;
	result = zwl_host_image_create(server->compose, width, height, server->compose->linear_sampler, &glass->blurred);
	if (result != VK_SUCCESS)
		return EIO;

	/* Its working copies. */
	small = calloc((size_t)width * height * 3U, sizeof(float));
	scratch = calloc((size_t)width * height * 3U, sizeof(float));
	if (small == NULL || scratch == NULL) {
		free(small);
		free(scratch);
		return ENOMEM;
	}

	/* Each small pixel is the average of the block it covers. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			for (dy = 0; dy < GLASS_BLUR_SCALE; dy++) {
				for (dx = 0; dx < GLASS_BLUR_SCALE; dx++) {
					at = ((size_t)(y * GLASS_BLUR_SCALE + dy) * server->width + x * GLASS_BLUR_SCALE + dx) * 3U;
					for (channel = 0; channel < 3U; channel++)
						small[((size_t)y * width + x) * 3U + channel] += pixels[at + channel];
				}
			}

			/* The sum becomes the average. */
			for (channel = 0; channel < 3U; channel++)
				small[((size_t)y * width + x) * 3U + channel] /= (float)(GLASS_BLUR_SCALE * GLASS_BLUR_SCALE);
		}
	}

	/* Box passes across and down. */
	for (pass = 0; pass < GLASS_BLUR_PASSES; pass++) {
		blur_pass(small, scratch, width, height, 1);
		blur_pass(small, scratch, width, height, 0);
	}

	/* Into the image. */
	for (y = 0; y < height; y++) {
		row = (uint32_t *)((unsigned char *)glass->blurred.map + y * glass->blurred.row_pitch);
		for (x = 0; x < width; x++)
			row[x] = pack_pixel(&small[((size_t)y * width + x) * 3U]);
	}

	/* Succeeded. */
	free(small);
	free(scratch);
	return 0;
}

/* Averages each pixel with its GLASS_BLUR_RADIUS neighbours on each side, across or down (clamped at the edges). */
static void
blur_pass(
	float *pixels,
	float *scratch,
	uint32_t width,
	uint32_t height,
	int horizontal)
{
	uint32_t x;
	uint32_t y;
	uint32_t channel;
	int32_t offset;
	int32_t sx;
	int32_t sy;
	float sum;

	/* Each output pixel from the input. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			for (channel = 0; channel < 3U; channel++) {
				sum = 0.0f;
				for (offset = -(int32_t)GLASS_BLUR_RADIUS; offset <= (int32_t)GLASS_BLUR_RADIUS; offset++) {
					/* The neighbour, clamped to the image. */
					sx = (int32_t)x;
					sy = (int32_t)y;
					if (horizontal)
						sx += offset;
					else
						sy += offset;
					if (sx < 0)
						sx = 0;
					if (sy < 0)
						sy = 0;
					if (sx >= (int32_t)width)
						sx = (int32_t)width - 1;
					if (sy >= (int32_t)height)
						sy = (int32_t)height - 1;
					sum += pixels[((size_t)sy * width + (size_t)sx) * 3U + channel];
				}

				/* The average of the window. */
				scratch[((size_t)y * width + x) * 3U + channel] = sum / (float)(2U * GLASS_BLUR_RADIUS + 1U);
			}
		}
	}

	/* The result replaces the input. */
	memcpy(pixels, scratch, (size_t)width * height * 3U * sizeof(float));
}

/* Packs a color (0..1 each) as an opaque BGRA pixel. */
static uint32_t
pack_pixel(
	const float *rgb)
{
	uint32_t red;
	uint32_t green;
	uint32_t blue;

	/* Each channel rounded to eight bits. */
	red = (uint32_t)(rgb[0] * 255.0f + 0.5f);
	green = (uint32_t)(rgb[1] * 255.0f + 0.5f);
	blue = (uint32_t)(rgb[2] * 255.0f + 0.5f);

	/* A, R, G, B from the top byte (B, G, R, A in memory). */
	return 0xff000000U | (red << 16) | (green << 8) | blue;
}

/*
 * Makes the glyph atlas from the font.  Returns 0 with text available, or an
 * error with the look to be drawn without text.
 */
static int
atlas_create(
	struct zwl_server *server,
	struct zwl_glass *glass)
{
	VkResult result;
	int error;

	/* The first font, kept open for the cache's glyphs. */
	error = glass_open_face(glass, server->font_path);
	if (error != 0)
		return error;

	/*
	 * The fallback font is opened on the first character the first font
	 * lacks (glass_cache_glyph): reading its megabytes here would delay
	 * zdesktop's start.
	 */
	glass->fallback_path = server->fallback_font_path;
	glass->fallback_tried = 0;

	/* The atlas image, transparent where nothing is drawn. */
	result = zwl_host_image_create(server->compose, GLASS_ATLAS_WIDTH, GLASS_ATLAS_HEIGHT, server->compose->sampler, &glass->atlas);
	if (result != VK_SUCCESS)
		return EIO;

	/* The glyphs at each size, the icons and the cache's place after them. */
	error = atlas_fill(glass, glass->faces[0]);
	if (error != 0)
		return error;

	/* Succeeded. */
	glass->text = 1;
	return 0;
}

/* Renders every glyph at every size into the atlas, row by row. */
static int
atlas_fill(
	struct zwl_glass *glass,
	struct truetype_face *face)
{
	static uint8_t bitmap[64U * 64U];
	struct truetype_glyph metrics;
	struct glass_glyph *glyph;
	uint32_t *row;
	uint32_t codepoint;
	uint32_t pen_x;
	uint32_t pen_y;
	uint32_t line;
	uint32_t x;
	uint32_t y;
	uint32_t value;
	unsigned size;
	unsigned index;
	unsigned id;
	int error;

	/* The pen starts at the top left; each row is as tall as its tallest glyph. */
	memset(glass->atlas.map, 0, glass->atlas.row_pitch * GLASS_ATLAS_HEIGHT);
	pen_x = 0;
	pen_y = 0;
	line = 0;
	for (size = 0; size < GLASS_SIZES; size++) {
		error = truetype_set_pixel_size(face, glass_pixels[size]);
		if (error != 0)
			return EINVAL;

		/* Each glyph of this size. */
		for (index = 0; index < GLASS_GLYPHS; index++) {
			codepoint = 32U + index;
			if (index == GLASS_CLOSE_GLYPH)
				codepoint = 0xd7U;
			if (index == GLASS_CHECK_GLYPH)
				codepoint = 0x2713U;
			if (index == GLASS_ARROW_GLYPH)
				codepoint = 0x203aU;
			id = truetype_glyph_index(face, codepoint);

			/* A menu sign the font lacks stays empty rather than showing the missing-glyph box. */
			if (id == 0U && index > GLASS_CLOSE_GLYPH)
				continue;
			error = truetype_glyph_metrics(face, id, &metrics);
			if (error != 0 || metrics.width > 64U || metrics.height > 64U)
				continue;

			/* A full row moves the pen down. */
			if (pen_x + metrics.width + 1U > GLASS_ATLAS_WIDTH) {
				pen_x = 0;
				pen_y += line + 1U;
				line = 0;
			}

			/* The atlas must hold it. */
			if (pen_y + metrics.height > GLASS_ATLAS_HEIGHT)
				return ENOSPC;

			/* The glyph's place and metrics. */
			glyph = &glass->glyphs[size][index];
			glyph->x = pen_x;
			glyph->y = pen_y;
			glyph->width = metrics.width;
			glyph->height = metrics.height;
			glyph->left = metrics.left;
			glyph->top = metrics.top;
			glyph->advance = metrics.advance;

			/* Its coverage as premultiplied white. */
			if (metrics.width != 0U && metrics.height != 0U) {
				memset(bitmap, 0, sizeof(bitmap));
				error = truetype_render_glyph(face, id, &metrics, bitmap, metrics.width, sizeof(bitmap));
				if (error != 0)
					continue;
				for (y = 0; y < metrics.height; y++) {
					row = (uint32_t *)((unsigned char *)glass->atlas.map + (pen_y + y) * glass->atlas.row_pitch);
					for (x = 0; x < metrics.width; x++) {
						value = bitmap[y * metrics.width + x];
						row[pen_x + x] = (value << 24) | (value << 16) | (value << 8) | value;
					}
				}
			}

			/* The pen moves past it. */
			pen_x += metrics.width + 1U;
			if (metrics.height > line)
				line = metrics.height;
		}
	}

	/* The icons in the rows after the glyphs. */
	pen_y += line + 1U;
	error = atlas_icons(glass, &pen_y);
	if (error != 0)
		return error;

	/* The cache's cells in the rest of the atlas. */
	glass->cache_top = pen_y;
	glass->cache_count = (GLASS_ATLAS_WIDTH / GLASS_CELL) * ((GLASS_ATLAS_HEIGHT - pen_y) / GLASS_CELL);
	if (glass->cache_count > GLASS_CELLS)
		glass->cache_count = GLASS_CELLS;
	printf("ZWL GLASS atlas cache-top=%u cells=%u faces=%u\n", glass->cache_top, glass->cache_count, glass->face_count);

	/* Succeeded. */
	return 0;
}

/* Reads a whole file of up to GLASS_FILE_MAX bytes; NULL with errno set when it cannot. */
static void *
file_read(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	ssize_t count;
	size_t length;
	int descriptor;

	/* The file. */
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return NULL;

	/* Its bytes, up to GLASS_FILE_MAX. */
	data = malloc(GLASS_FILE_MAX);
	if (data == NULL) {
		close(descriptor);
		errno = ENOMEM;
		return NULL;
	}

	/* Read to the end or the bound. */
	length = 0;
	for (;;) {
		count = read(descriptor, data + length, GLASS_FILE_MAX - length);
		if (count <= 0)
			break;
		length += (size_t)count;
		if (length == GLASS_FILE_MAX)
			break;
	}

	/* The file is no longer needed. */
	close(descriptor);

	/* An empty or unreadable file has nothing to use. */
	if (length == 0) {
		free(data);
		errno = EINVAL;
		return NULL;
	}

	/* Succeeded. */
	*size = length;
	return data;
}

/*
 * Reads a binary PPM (P6, maximum 255) as the wallpaper, scaled to the
 * output by the nearest pixel.  Returns the colors (0..1, three per pixel),
 * or NULL with errno set.
 */
static float *
wallpaper_load(
	const char *path,
	uint32_t width,
	uint32_t height)
{
	const unsigned char *pixel;
	unsigned char *data;
	float *pixels;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t maximum;
	uint32_t x;
	uint32_t y;
	size_t size;
	size_t at;
	int error;

	/* The file, with the P6 magic. */
	data = file_read(path, &size);
	if (data == NULL)
		return NULL;
	if (size < 2U || data[0] != 'P' || data[1] != '6') {
		free(data);
		errno = EINVAL;
		return NULL;
	}

	/* The width, the height and the maximum value. */
	at = 2U;
	error = ppm_number(data, size, &at, &source_width);
	if (error == 0)
		error = ppm_number(data, size, &at, &source_height);
	if (error == 0)
		error = ppm_number(data, size, &at, &maximum);
	if (error != 0 || maximum != 255U || source_width == 0U || source_height == 0U) {
		free(data);
		errno = EINVAL;
		return NULL;
	}

	/* One whitespace byte, then three bytes a pixel. */
	at++;
	if (at > size || (size - at) / 3U / source_width < source_height) {
		free(data);
		errno = EINVAL;
		return NULL;
	}

	/* The output's pixels, each from the nearest source pixel. */
	pixels = malloc((size_t)width * height * 3U * sizeof(float));
	if (pixels == NULL) {
		free(data);
		errno = ENOMEM;
		return NULL;
	}

	/* Each output pixel takes the source pixel it falls on. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			pixel = data + at + ((size_t)(y * source_height / height) * source_width + (size_t)(x * source_width / width)) * 3U;
			pixels[((size_t)y * width + x) * 3U] = (float)pixel[0] / 255.0f;
			pixels[((size_t)y * width + x) * 3U + 1U] = (float)pixel[1] / 255.0f;
			pixels[((size_t)y * width + x) * 3U + 2U] = (float)pixel[2] / 255.0f;
		}
	}

	/* Succeeded. */
	free(data);
	return pixels;
}

/* Reads one decimal number of a PPM header, after whitespace and comments. */
static int
ppm_number(
	const unsigned char *data,
	size_t size,
	size_t *at,
	uint32_t *number)
{
	uint32_t value;
	unsigned digits;

	/* Whitespace, and comments to the end of their line. */
	while (*at < size) {
		if (data[*at] == '#') {
			while (*at < size && data[*at] != '\n')
				(*at)++;
			continue;
		}

		/* The first byte that is not whitespace starts the number. */
		if (data[*at] != ' ' && data[*at] != '\t' && data[*at] != '\r' && data[*at] != '\n')
			break;
		(*at)++;
	}

	/* The digits, within a bound. */
	value = 0;
	digits = 0;
	while (*at < size && data[*at] >= '0' && data[*at] <= '9' && digits < 6U) {
		value = value * 10U + (uint32_t)(data[*at] - '0');
		(*at)++;
		digits++;
	}

	/* A number has at least one digit. */
	if (digits == 0U)
		return EINVAL;

	/* Succeeded. */
	*number = value;
	return 0;
}

/* Starts a shape over a box: the quad is the box, with no image and no color. */
void
glass_shape_init(
	struct glass_shape *shape,
	float x,
	float y,
	float width,
	float height)
{
	/* The quad and the box are the same until the caller widens the quad. */
	memset(shape, 0, sizeof(*shape));
	shape->quad[0] = x;
	shape->quad[1] = y;
	shape->quad[2] = width;
	shape->quad[3] = height;
	shape->box[0] = x;
	shape->box[1] = y;
	shape->box[2] = width;
	shape->box[3] = height;

	/* The whole image, not faded. */
	shape->uv[2] = 1.0f;
	shape->uv[3] = 1.0f;
	shape->opacity = 1.0f;
}

/* Records one shape: its constants for both shader stages and the strip. */
void
glass_shape_draw(
	struct zwl_server *server,
	VkCommandBuffer command,
	const struct glass_shape *shape)
{
	struct zwl_compose *compose;
	float constants[ZWL_PANEL_CONSTANTS];
	float quad[4];
	float box[4];
	float radius;
	float width;
	float height;
	VkDescriptorSet set;

	/*
	 * The quad and the box, moved and scaled when the desktop layer is
	 * pushed aside by App Home (home.c): x' = layer x + x * scale.
	 */
	compose = server->compose;
	width = (float)server->width;
	height = (float)server->height;
	memcpy(quad, shape->quad, sizeof(quad));
	memcpy(box, shape->box, sizeof(box));
	radius = shape->radius;
	if (server->layer_on) {
		quad[0] = server->layer_x + quad[0] * server->layer_scale;
		quad[1] = server->layer_y + quad[1] * server->layer_scale;
		quad[2] = quad[2] * server->layer_scale;
		quad[3] = quad[3] * server->layer_scale;
		box[0] = server->layer_x + box[0] * server->layer_scale;
		box[1] = server->layer_y + box[1] * server->layer_scale;
		box[2] = box[2] * server->layer_scale;
		box[3] = box[3] * server->layer_scale;
		radius = radius * server->layer_scale;
	}

	/* The quad in normalized device coordinates. */
	constants[0] = 2.0f * quad[0] / width - 1.0f;
	constants[1] = 2.0f * quad[1] / height - 1.0f;
	constants[2] = 2.0f * (quad[0] + quad[2]) / width - 1.0f;
	constants[3] = 2.0f * (quad[1] + quad[3]) / height - 1.0f;

	/* The part of the image, the box, the color. */
	memcpy(&constants[4], shape->uv, sizeof(shape->uv));
	memcpy(&constants[8], box, sizeof(box));
	memcpy(&constants[12], shape->color, sizeof(shape->color));

	/* The shape and the output. */
	constants[16] = radius;
	constants[17] = shape->mode;
	constants[18] = shape->soft;
	constants[19] = shape->opaque;
	constants[20] = width;
	constants[21] = height;
	constants[22] = shape->edge;
	constants[23] = shape->opacity;

	/*
	 * A shape without an image of its own is given the blurred scene under
	 * the window being drawn when there is one (backdrop.c), else the
	 * blurred wallpaper (only the glass samples it).
	 */
	set = shape->set;
	if (set == VK_NULL_HANDLE)
		set = compose->backdrop_set;
	if (set == VK_NULL_HANDLE)
		set = compose->glass->blurred.set;

	/* The pipeline, the image and the strip. */
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, compose->panel_pipeline);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, compose->panel_layout, 0U, 1U, &set, 0U, NULL);
	vkCmdPushConstants(command, compose->panel_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(constants), constants);
	vkCmdDraw(command, ZWL_QUAD_VERTICES, 1U, 0U, 0U);
}

/* Draws a rounded rectangle in a solid color. */
void
glass_draw_solid(
	struct zwl_server *server,
	VkCommandBuffer command,
	float x,
	float y,
	float width,
	float height,
	float radius,
	const float *color)
{
	struct glass_shape shape;

	/* One solid shape. */
	glass_shape_init(&shape, x, y, width, height);
	shape.mode = MODE_SOLID;
	shape.radius = radius;
	memcpy(shape.color, color, sizeof(shape.color));
	glass_shape_draw(server, command, &shape);
}

/* The width of a line of UTF-8 text in pixels (0 without text). */
int32_t
glass_text_width(
	struct zwl_server *server,
	enum glass_size size,
	const char *text)
{
	const struct glass_glyph *glyph;
	struct zwl_glass *glass;
	uint32_t codepoint;
	int32_t width;

	/* Nothing without glyphs. */
	glass = server->compose->glass;
	width = 0;
	if (!glass->text)
		return 0;

	/* The sum of the advances of the characters there are glyphs for. */
	while (*text != '\0') {
		codepoint = glass_utf8_next(&text);
		glyph = glass_glyph_of(glass, size, codepoint);
		if (glyph != NULL)
			width += glyph->advance;
	}

	/* Succeeded. */
	return width;
}

/*
 * Draws a line of UTF-8 text from x on a baseline, cut short (with an
 * ellipsis of dots) where it would pass x + limit.
 */
void
glass_draw_text(
	struct zwl_server *server,
	VkCommandBuffer command,
	enum glass_size size,
	int32_t x,
	int32_t baseline,
	const char *text,
	int32_t limit,
	const float *color)
{
	const struct glass_glyph *glyph;
	struct zwl_glass *glass;
	uint32_t codepoint;
	int32_t start;
	int32_t width;
	int32_t dots;
	unsigned index;

	/* Nothing without glyphs. */
	glass = server->compose->glass;
	if (!glass->text)
		return;

	/* Room is kept for the ellipsis when the text is too long. */
	dots = 3 * glass->glyphs[size]['.' - 32].advance;
	width = glass_text_width(server, size, text);
	if (width <= limit)
		dots = 0;

	/* Each character there is a glyph for, while there is room. */
	start = x;
	while (*text != '\0') {
		codepoint = glass_utf8_next(&text);
		glyph = glass_glyph_of(glass, size, codepoint);
		if (glyph == NULL)
			continue;

		/* The text stops where the ellipsis must go. */
		if (dots != 0 && x + glyph->advance > start + limit - dots)
			break;

		/* The glyph, and the pen after it. */
		glass_draw_glyph_at(server, command, glyph, x, baseline, color);
		x += glyph->advance;
	}

	/* The ellipsis. */
	if (dots != 0) {
		for (index = 0; index < 3U; index++) {
			glass_draw_glyph(server, command, size, '.' - 32, x, baseline, color);
			x += glass->glyphs[size]['.' - 32].advance;
		}
	}
}

/*
 * Draws a line of UTF-8 text from x on a baseline, cut in the middle (with
 * an ellipsis of dots between its start and its end) where it would pass
 * x + limit.  Names that differ only at their end (Document 1, Document 2)
 * stay told apart.
 */
void
glass_draw_text_middle(
	struct zwl_server *server,
	VkCommandBuffer command,
	enum glass_size size,
	int32_t x,
	int32_t baseline,
	const char *text,
	int32_t limit,
	const float *color)
{
	const struct glass_glyph *glyph;
	struct zwl_glass *glass;
	const char *tail;
	const char *next;
	uint32_t codepoint;
	int32_t width;
	int32_t dots;
	int32_t room;
	int32_t before;
	int32_t tail_width;
	int32_t head_end;
	unsigned index;

	/* Nothing without glyphs. */
	glass = server->compose->glass;
	if (!glass->text)
		return;

	/* A text that fits is drawn whole. */
	width = glass_text_width(server, size, text);
	if (width <= limit) {
		glass_draw_text(server, command, size, x, baseline, text, limit, color);
		return;
	}

	/* The room besides the ellipsis; without any, the ordinary cut. */
	dots = 3 * glass->glyphs[size]['.' - 32].advance;
	room = limit - dots;
	if (room <= 0) {
		glass_draw_text(server, command, size, x, baseline, text, limit, color);
		return;
	}

	/* The end kept: the last characters within half the room (before is the width ahead of it). */
	before = 0;
	next = text;
	while (*next != '\0') {
		/* The rest from here fits in half the room. */
		if (width - before <= room / 2)
			break;

		/* Otherwise this character is ahead of the end. */
		codepoint = glass_utf8_next(&next);
		glyph = glass_glyph_of(glass, size, codepoint);
		if (glyph != NULL)
			before += glyph->advance;
	}

	/* The end starts there. */
	tail = next;

	/* An end that starts with a dot starts after it (the ellipsis would run into it: "REA....md"). */
	if (*tail == '.') {
		codepoint = glass_utf8_next(&next);
		glyph = glass_glyph_of(glass, size, codepoint);
		if (glyph != NULL)
			before += glyph->advance;
		tail = next;
	}

	/* The start, while it fits before the ellipsis and the end. */
	tail_width = width - before;
	head_end = x + room - tail_width;
	next = text;
	while (next < tail) {
		/* The next character there is a glyph for. */
		codepoint = glass_utf8_next(&next);
		glyph = glass_glyph_of(glass, size, codepoint);
		if (glyph == NULL)
			continue;

		/* The start stops where the ellipsis must go. */
		if (x + glyph->advance > head_end)
			break;

		/* The glyph, and the pen after it. */
		glass_draw_glyph_at(server, command, glyph, x, baseline, color);
		x += glyph->advance;
	}

	/* The ellipsis. */
	for (index = 0; index < 3U; index++) {
		glass_draw_glyph(server, command, size, '.' - 32, x, baseline, color);
		x += glass->glyphs[size]['.' - 32].advance;
	}

	/* The end, each character there is a glyph for. */
	next = tail;
	while (*next != '\0') {
		/* The next character's glyph. */
		codepoint = glass_utf8_next(&next);
		glyph = glass_glyph_of(glass, size, codepoint);
		if (glyph == NULL)
			continue;

		/* The glyph, and the pen after it. */
		glass_draw_glyph_at(server, command, glyph, x, baseline, color);
		x += glyph->advance;
	}
}

/* Draws one glyph of the atlas with its origin at x on the baseline. */
void
glass_draw_glyph(
	struct zwl_server *server,
	VkCommandBuffer command,
	enum glass_size size,
	unsigned index,
	int32_t x,
	int32_t baseline,
	const float *color)
{
	struct zwl_glass *glass;

	/* The atlas's glyph, drawn where it goes. */
	glass = server->compose->glass;
	glass_draw_glyph_at(server, command, &glass->glyphs[size][index], x, baseline, color);
}

/* The advance of one glyph of the atlas (0 without text). */
int32_t
glass_glyph_advance(
	struct zwl_server *server,
	enum glass_size size,
	unsigned index)
{
	struct zwl_glass *glass;

	/* Nothing without glyphs. */
	glass = server->compose->glass;
	if (!glass->text)
		return 0;

	/* Succeeded. */
	return glass->glyphs[size][index].advance;
}

/*
 * Draws a titlebar icon (GLASS_ICON_*) in a square of a size in pixels at
 * (x, y): the atlas's icon of that size, or of the nearest one scaled.
 */
void
glass_draw_icon(
	struct zwl_server *server,
	VkCommandBuffer command,
	unsigned icon,
	int32_t x,
	int32_t y,
	unsigned pixels,
	const float *color)
{
	struct glass_shape shape;
	const struct glass_glyph *glyph;
	struct zwl_glass *glass;
	unsigned size;

	/* Nothing without the atlas, or for an icon there is not. */
	glass = server->compose->glass;
	if (!glass->text || icon >= GLASS_ICON_COUNT)
		return;

	/* The larger size for anything above the smaller. */
	size = 0;
	if (pixels > glass_icon_pixels[0])
		size = 1;
	glyph = &glass->icons[size][icon];

	/* The icon's cell of the atlas, over the square. */
	glass_shape_init(&shape, (float)x, (float)y, (float)pixels, (float)pixels);
	shape.mode = MODE_TEXT;
	shape.uv[0] = (float)glyph->x / (float)GLASS_ATLAS_WIDTH;
	shape.uv[1] = (float)glyph->y / (float)GLASS_ATLAS_HEIGHT;
	shape.uv[2] = (float)(glyph->x + glyph->width) / (float)GLASS_ATLAS_WIDTH;
	shape.uv[3] = (float)(glyph->y + glyph->height) / (float)GLASS_ATLAS_HEIGHT;
	memcpy(shape.color, color, sizeof(shape.color));
	shape.set = glass->atlas.set;
	glass_shape_draw(server, command, &shape);
}

/* The descriptor set of the wallpaper, for pictures of it (the desktops). */
VkDescriptorSet
glass_wallpaper_set(
	struct zwl_server *server)
{
	/* The full-size image. */
	return server->compose->glass->wallpaper.set;
}

/*
 * Renders every icon at each of its sizes into the atlas from a row on,
 * and moves the row past them; returns 0, or ENOSPC when the atlas is full.
 */
static int
atlas_icons(
	struct zwl_glass *glass,
	uint32_t *pen_y)
{
	static uint8_t bitmap[GLASS_BITMAP * GLASS_BITMAP];
	struct glass_glyph *glyph;
	uint32_t pen_x;
	unsigned pixels;
	unsigned tallest;
	unsigned size;
	unsigned icon;

	/* The icons side by side, as tall as the largest size. */
	pen_x = 0;
	tallest = glass_icon_pixels[GLASS_ICON_SIZES - 1U];
	for (size = 0; size < GLASS_ICON_SIZES; size++) {
		pixels = glass_icon_pixels[size];
		for (icon = 0; icon < GLASS_ICON_COUNT; icon++) {
			/* A full row moves the pen down. */
			if (pen_x + pixels + 1U > GLASS_ATLAS_WIDTH) {
				pen_x = 0;
				*pen_y += tallest + 1U;
			}

			/* The atlas must hold it. */
			if (*pen_y + pixels > GLASS_ATLAS_HEIGHT)
				return ENOSPC;

			/* The icon's coverage, into the atlas. */
			zwl_icon_raster(icon, pixels, bitmap, pixels);
			atlas_put(glass, bitmap, pen_x, *pen_y, pixels, pixels);

			/* Its place, square, drawn from its top left. */
			glyph = &glass->icons[size][icon];
			glyph->x = pen_x;
			glyph->y = *pen_y;
			glyph->width = pixels;
			glyph->height = pixels;
			glyph->left = 0;
			glyph->top = 0;
			glyph->advance = (int32_t)pixels;

			/* The pen moves past it. */
			pen_x += pixels + 1U;
		}
	}

	/* The row after the icons. */
	*pen_y += tallest + 1U;

	/* Succeeded: the icons are in the atlas. */
	return 0;
}

/* Writes a bitmap of coverage into the atlas at a place, as premultiplied white. */
static void
atlas_put(
	struct zwl_glass *glass,
	const uint8_t *bitmap,
	uint32_t x,
	uint32_t y,
	uint32_t width,
	uint32_t height)
{
	uint32_t *row;
	uint32_t value;
	uint32_t column;
	uint32_t line;

	/* Each row of the bitmap, onto its row of the atlas. */
	for (line = 0; line < height; line++) {
		row = (uint32_t *)((unsigned char *)glass->atlas.map + (size_t)(y + line) * glass->atlas.row_pitch);
		for (column = 0; column < width; column++) {
			value = bitmap[line * width + column];
			row[x + column] = (value << 24) | (value << 16) | (value << 8) | value;
		}
	}
}

/*
 * Opens a font and keeps it (and its file's bytes) for the cache; returns
 * 0, ENOENT for no path, or the reason it could not be read.
 */
static int
glass_open_face(
	struct zwl_glass *glass,
	const char *path)
{
	struct truetype_face *face;
	void *data;
	size_t size;
	int error;

	/* No path, or no room for another font. */
	if (path == NULL || glass->face_count == GLASS_FACES)
		return ENOENT;

	/* The file. */
	data = file_read(path, &size);
	if (data == NULL)
		return errno;

	/* The font in it. */
	error = truetype_open(data, size, 0U, &face);
	if (error != 0) {
		free(data);
		return EINVAL;
	}

	/* Kept until the look closes. */
	glass->faces[glass->face_count] = face;
	glass->font_data[glass->face_count] = data;
	glass->face_count++;

	/* Succeeded: the font can render glyphs. */
	return 0;
}

/* Finds the glyph of a character at a size: the atlas's for ASCII, the cache's for any other; NULL for none. */
static const struct glass_glyph *
glass_glyph_of(
	struct zwl_glass *glass,
	enum glass_size size,
	uint32_t codepoint)
{
	const struct glass_glyph *glyph;

	/* Control characters have none. */
	if (codepoint < 32U || codepoint == 127U)
		return NULL;

	/* Printable ASCII is in the atlas. */
	if (codepoint < 127U)
		return &glass->glyphs[size][codepoint - 32U];

	/* Any other character is in the cache, rendered on its first use. */
	glyph = glass_cache_glyph(glass, size, codepoint);
	return glyph;
}

/*
 * Finds a character's glyph in the cache, rendering it into the least
 * recently drawn cell when it is not there; NULL when it cannot be.
 */
static const struct glass_glyph *
glass_cache_glyph(
	struct zwl_glass *glass,
	enum glass_size size,
	uint32_t codepoint)
{
	static uint8_t bitmap[GLASS_BITMAP * GLASS_BITMAP];
	struct truetype_glyph metrics;
	struct truetype_face *face;
	struct glass_cached *cell;
	unsigned per_row;
	unsigned oldest;
	unsigned index;
	unsigned which;
	unsigned id;
	int error;

	/* No cache without room or fonts. */
	if (glass->cache_count == 0U || glass->face_count == 0U)
		return NULL;

	/* The cell that holds it already, else the least recently drawn (a free one first). */
	oldest = 0;
	for (index = 0; index < glass->cache_count; index++) {
		cell = &glass->cache[index];
		if (cell->codepoint == codepoint && cell->size == (unsigned)size) {
			glass->clock++;
			cell->used = glass->clock;
			return &cell->glyph;
		}

		/* Otherwise the oldest so far. */
		if (cell->used < glass->cache[oldest].used)
			oldest = index;
	}

	/* The first font that has the character, else the first font's missing-glyph box. */
	face = glass->faces[0];
	id = 0;
	for (which = 0; which < glass->face_count; which++) {
		id = truetype_glyph_index(glass->faces[which], codepoint);
		if (id != 0U) {
			face = glass->faces[which];
			break;
		}
	}

	/* No font open has it: the fallback font, opened once on the first such character. */
	if (id == 0U && glass->fallback_tried == 0U) {
		glass->fallback_tried = 1;
		error = glass_open_face(glass, glass->fallback_path);
		if (error != 0) {
			printf("ZWL GLASS no fallback font: path=%s errno=%d\n", glass->fallback_path, error);
		} else {
			printf("ZWL GLASS fallback font: path=%s faces=%u\n", glass->fallback_path, glass->face_count);
			which = glass->face_count - 1U;
			id = truetype_glyph_index(glass->faces[which], codepoint);
			if (id != 0U)
				face = glass->faces[which];
		}
	}

	/* No font has it: the first font's box. */
	if (id == 0U)
		which = 0;

	/* Its metrics at the size, which must fit a cell. */
	error = truetype_set_pixel_size(face, glass_pixels[size]);
	if (error != 0)
		return NULL;
	error = truetype_glyph_metrics(face, id, &metrics);
	if (error != 0)
		return NULL;
	if (metrics.width > GLASS_CELL || metrics.height > GLASS_CELL)
		return NULL;

	/* The cell's place in the atlas. */
	cell = &glass->cache[oldest];
	per_row = GLASS_ATLAS_WIDTH / GLASS_CELL;
	cell->glyph.x = (oldest % per_row) * GLASS_CELL;
	cell->glyph.y = glass->cache_top + (oldest / per_row) * GLASS_CELL;

	/* The glyph's coverage, into the cell. */
	memset(bitmap, 0, sizeof(bitmap));
	if (metrics.width != 0U && metrics.height != 0U) {
		error = truetype_render_glyph(face, id, &metrics, bitmap, metrics.width, sizeof(bitmap));
		if (error != 0)
			return NULL;
	}

	/* Into the cell. */
	atlas_put(glass, bitmap, cell->glyph.x, cell->glyph.y, metrics.width, metrics.height);

	/* The cell now holds this character's glyph. */
	cell->codepoint = codepoint;
	cell->size = (unsigned)size;
	cell->glyph.width = metrics.width;
	cell->glyph.height = metrics.height;
	cell->glyph.left = metrics.left;
	cell->glyph.top = metrics.top;
	cell->glyph.advance = metrics.advance;
	glass->clock++;
	cell->used = glass->clock;
	printf("ZWL GLASS glyph codepoint=U+%04X size=%u face=%u cell=%u\n", codepoint, (unsigned)size, which, oldest);

	/* Succeeded: the glyph. */
	return &cell->glyph;
}

/* Reads one UTF-8 character and moves past it; a malformed byte reads as U+FFFD and is passed alone. */
static uint32_t
glass_utf8_next(
	const char **text)
{
	const unsigned char *bytes;
	uint32_t codepoint;
	unsigned count;
	unsigned index;

	/* The first byte says how many follow. */
	bytes = (const unsigned char *)*text;
	if (bytes[0] < 0x80U) {
		*text += 1;
		return bytes[0];
	}

	/* A lead byte of two, three or four. */
	if ((bytes[0] & 0xe0U) == 0xc0U) {
		codepoint = bytes[0] & 0x1fU;
		count = 1;
	} else if ((bytes[0] & 0xf0U) == 0xe0U) {
		codepoint = bytes[0] & 0x0fU;
		count = 2;
	} else if ((bytes[0] & 0xf8U) == 0xf0U) {
		codepoint = bytes[0] & 0x07U;
		count = 3;
	} else {
		*text += 1;
		return 0xfffdU;
	}

	/* Each continuation byte adds six bits; a missing one makes the lead byte malformed. */
	for (index = 1; index <= count; index++) {
		if ((bytes[index] & 0xc0U) != 0x80U) {
			*text += 1;
			return 0xfffdU;
		}

		/* The byte's bits. */
		codepoint = (codepoint << 6) | (bytes[index] & 0x3fU);
	}

	/* Succeeded: the character, and the text after it. */
	*text += 1U + count;
	return codepoint;
}

/* Draws one glyph with its origin at x on the baseline, one to one with the atlas. */
static void
glass_draw_glyph_at(
	struct zwl_server *server,
	VkCommandBuffer command,
	const struct glass_glyph *glyph,
	int32_t x,
	int32_t baseline,
	const float *color)
{
	struct glass_shape shape;
	struct zwl_glass *glass;

	/* A space draws nothing. */
	glass = server->compose->glass;
	if (glyph->width == 0U || glyph->height == 0U)
		return;

	/* The glyph's pixels. */
	glass_shape_init(&shape, (float)(x + glyph->left), (float)(baseline - glyph->top), (float)glyph->width, (float)glyph->height);
	shape.mode = MODE_TEXT;
	shape.uv[0] = (float)glyph->x / (float)GLASS_ATLAS_WIDTH;
	shape.uv[1] = (float)glyph->y / (float)GLASS_ATLAS_HEIGHT;
	shape.uv[2] = (float)(glyph->x + glyph->width) / (float)GLASS_ATLAS_WIDTH;
	shape.uv[3] = (float)(glyph->y + glyph->height) / (float)GLASS_ATLAS_HEIGHT;
	memcpy(shape.color, color, sizeof(shape.color));
	shape.set = glass->atlas.set;
	glass_shape_draw(server, command, &shape);
}
