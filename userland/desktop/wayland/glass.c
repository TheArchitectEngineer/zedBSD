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
 * A character neither font has is looked for in the colour emoji font
 * (GLASS_EMOJI_FONT, opened on the first such character, ws102-p019),
 * whose colour glyph goes into the cell as it is and is drawn as an image.
 * The titlebar's icons (icons.c) are rendered into the atlas once, at two
 * sizes, App Home's pictures (icons.c, ws035-p123) at the size its tiles
 * draw them and small for the windows' marks (ws035-p124), and so are the
 * seven layers of the Kei mark (artwork/mark.c,
 * ws035-p108), which the login and lock screens draw.
 */

#include "glass.h"
#include "../artwork/mark.h"
#include "../picture/color-glyph.h"

#include <truetype.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/* The fonts kept open: the first font, the fallback and the colour emoji font (ws102-p019). */
#define GLASS_FACES		3U
#define GLASS_EMOJI_FONT	"/usr/share/fonts/keiland-emoji.ttf"

/* The icons' two sizes in pixels, and how many there are. */
#define GLASS_ICON_SIZES	2U

/*
 * App Home's pictures, in pixels a side (on its 72-pixel tiles), and again
 * small for a window's mark by its title (a 20-pixel square, ws035-p124).
 */
#define GLASS_APP_ICON_PIXELS	40U
#define GLASS_APP_ICON_SMALL	14U

/* A cached glyph's cell in the atlas, in pixels a side, and the most cells there are. */
#define GLASS_CELL		48U
#define GLASS_CELLS		512U

/* The Kei mark's layers in the atlas, in pixels a side. */
#define GLASS_MARK_PIXELS	128U

/*
 * The Kei mark's layers again at the system bar's launcher size, in pixels a
 * side (ws035-p117): drawn one to one, they stay crisp where the large ones
 * would be shrunk five times.  They sit in the free column right of the
 * large ones, GLASS_MARK_SMALL_ACROSS a row.
 */
#define GLASS_MARK_SMALL_PIXELS	26U
#define GLASS_MARK_SMALL_ACROSS	4U

/* The largest glyph rendered, in pixels a side. */
#define GLASS_BITMAP		64U

/* The blurred wallpaper is this many times smaller than the output. */
#define GLASS_BLUR_SCALE	4U
#define GLASS_BLUR_RADIUS	6U
#define GLASS_BLUR_PASSES	3U

/*
 * A binary PPM read for the wallpaper (ws035-p133): the file's bytes, where
 * its pixels (three bytes each) start in them, and its size in pixels.
 */
struct wallpaper_picture {
	unsigned char *data;
	size_t pixels;
	uint32_t width;
	uint32_t height;
};

/*
 * The wallpaper's file read ahead on a thread of its own (ws035-p133), so
 * that the disk works while the Vulkan device is made: the path, the bytes
 * and their size (NULL when the read failed), and whether the thread runs or
 * has not been joined yet.  Only the main thread starts and joins it; the
 * thread writes data and size before it ends, and pthread_join makes them
 * visible to the main thread.
 */
struct glass_prefetch {
	pthread_t thread;
	const char *path;
	unsigned char *data;
	size_t size;
	int started;
};

struct glass_glyph {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	int32_t left;
	int32_t top;
	int32_t advance;
	uint32_t color;
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
 * the ASCII glyphs at each size, the icons at their sizes (App Home's
 * pictures at a size of their own and small), the Kei mark's
 * layers large and at the launcher's size (ws035-p117), and the cache of
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
	struct glass_glyph app_icons[GLASS_ICON_APPS];
	struct glass_glyph app_icons_small[GLASS_ICON_APPS];
	struct glass_glyph mark[KEILAND_MARK_LAYERS];
	struct glass_glyph mark_small[KEILAND_MARK_LAYERS];
	unsigned text;
	struct truetype_face *faces[GLASS_FACES];
	void *font_data[GLASS_FACES];
	unsigned face_count;
	const char *fallback_path;
	unsigned fallback_tried;
	unsigned emoji_tried;
	int emoji_face;
	struct glass_cached cache[GLASS_CELLS];
	uint32_t cache_top;
	unsigned cache_count;
	uint64_t clock;
};

static const unsigned glass_pixels[GLASS_SIZES] = { 14U, 15U, 20U, 36U, 24U };

/* The icons' sizes in pixels. */
static const unsigned glass_icon_pixels[GLASS_ICON_SIZES] = { 16U, 20U };

static int wallpaper_create(struct zwl_server *server, struct zwl_glass *glass);
static int wallpaper_fill(struct zwl_server *server, struct zwl_glass *glass, const char *path);
static void wallpaper_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float *rgb);
static float ridge(float x, float base, float amplitude, float phase);
static int blur_create(struct zwl_server *server, struct zwl_glass *glass);
static void wallpaper_row(const struct wallpaper_picture *picture, const uint32_t *columns, uint32_t source_y, uint32_t *line, uint32_t width);
static void landscape_row(uint32_t y, uint32_t width, uint32_t height, uint32_t *line);
static void blur_add(const uint32_t *line, uint32_t *sums, uint32_t small_width);
static void blur_average(uint32_t *sums, float *small, uint32_t small_width);
static int blur_fill(struct zwl_server *server, struct zwl_glass *glass, float *small);
static void blur_pass(float *pixels, float *scratch, uint32_t width, uint32_t height, int horizontal);
static uint32_t pack_pixel(const float *rgb);
static int atlas_create(struct zwl_server *server, struct zwl_glass *glass);
static int atlas_fill(struct zwl_glass *glass, struct truetype_face *face);
static int atlas_icons(struct zwl_glass *glass, uint32_t *pen_y);
static int atlas_mark(struct zwl_glass *glass, uint32_t *pen_y);
static void atlas_put(struct zwl_glass *glass, const uint8_t *bitmap, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
static int glass_open_face(struct zwl_glass *glass, const char *path);
static const struct glass_glyph *glass_glyph_of(struct zwl_glass *glass, enum glass_size size, uint32_t codepoint);
static const struct glass_glyph *glass_cache_glyph(struct zwl_glass *glass, enum glass_size size, uint32_t codepoint);
static const struct glass_glyph *glass_cache_color(struct zwl_glass *glass, enum glass_size size, uint32_t codepoint, unsigned id, unsigned slot);
static uint32_t glass_utf8_next(const char **text);
static void glass_draw_glyph_at(struct zwl_server *server, VkCommandBuffer command, const struct glass_glyph *glyph, int32_t x, int32_t baseline, const float *color);
static void *file_read(const char *path, size_t *size);
static int wallpaper_load(const char *path, struct wallpaper_picture *picture);
static int ppm_number(const unsigned char *data, size_t size, size_t *at, uint32_t *number);
static void *prefetch_run(void *argument);
static unsigned char *prefetch_take(const char *path, size_t *size);

/* The wallpaper read ahead, when zwl_glass_prefetch started it (only the main thread starts and takes it). */
static struct glass_prefetch glass_prefetch;

/*
 * Starts reading the wallpaper's file on a thread (ws035-p133), before the
 * Vulkan device is made; the look takes the bytes when it draws the
 * wallpaper.  Without a thread the look reads the file itself.
 */
void
zwl_glass_prefetch(
	struct zwl_server *server)
{
	int error;

	/* Only the glass look with a picture reads one. */
	if (!server->glass || server->wallpaper_path == NULL)
		return;

	/* The thread; without it the file is read when the wallpaper is drawn. */
	glass_prefetch.path = server->wallpaper_path;
	error = pthread_create(&glass_prefetch.thread, NULL, prefetch_run, NULL);
	if (error != 0) {
		printf("ZWL GLASS prefetch unavailable errno=%d\n", error);
		return;
	}

	/* The thread runs until the look takes its bytes. */
	glass_prefetch.started = 1;
}

/*
 * Makes the wallpaper, its blurred copy and the glyph atlas.
 */
int
zwl_glass_open(
	struct zwl_server *server)
{
	struct zwl_glass *glass;
	uint64_t started;
	int error;

	/* The look's state lives as long as window mode's device. */
	glass = calloc(1, sizeof(*glass));
	if (glass == NULL)
		return ENOMEM;
	server->compose->glass = glass;

	/* The wallpaper and the frosted glass made from it. */
	started = zwl_milliseconds();
	error = wallpaper_create(server, glass);
	if (error != 0)
		return error;
	printf("ZWL STARTUP step=wallpaper ms=%llu\n", (unsigned long long)(zwl_milliseconds() - started));

	/* The glyphs; the look is drawn without text when the font cannot be read. */
	started = zwl_milliseconds();
	error = atlas_create(server, glass);
	if (error != 0)
		printf("ZWL GLASS no text: font=%s errno=%d\n", server->font_path, error);
	printf("ZWL STARTUP step=glyphs ms=%llu\n", (unsigned long long)(zwl_milliseconds() - started));

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

/*
 * Draws another wallpaper into the look (ws089-p007, the desktop's
 * preferences): a binary PPM, or with path NULL the landscape drawn
 * here.
 *
 * The images keep their size and their descriptors, so only their pixels
 * change; the device finishes what it is drawing from them first.  The
 * whole output is drawn again.  Returns 0 or an errno value.
 */
int
zwl_glass_wallpaper(
	struct zwl_server *server,
	const char *path)
{
	const char *shown;
	uint64_t started;
	int error;

	/* Without the look there is no wallpaper. */
	if (server->compose == NULL || server->compose->glass == NULL)
		return ENODEV;

	/* The frames in flight read the images; they end first. */
	started = zwl_milliseconds();
	(void)vkDeviceWaitIdle(server->compose->device);

	/* The picture asked for. */
	error = wallpaper_fill(server, server->compose->glass, path);
	if (error != 0)
		return error;

	/* Everything on the output stands on it. */
	server->dirty = 1;

	/* The log names the picture ("-" for the landscape) and how long it took. */
	shown = "-";
	if (path != NULL)
		shown = path;
	printf("ZWL GLASS wallpaper path=%s ms=%llu\n", shown, (unsigned long long)(zwl_milliseconds() - started));

	/* Succeeded: the new wallpaper is shown from the next frame. */
	return 0;
}

/* Makes the wallpaper's image and its blurred copy, and draws them. */
static int
wallpaper_create(
	struct zwl_server *server,
	struct zwl_glass *glass)
{
	VkResult result;
	int error;

	/* The output-sized image. */
	result = zwl_host_image_create(server->compose, server->width, server->height, server->compose->sampler, &glass->wallpaper);
	if (result != VK_SUCCESS)
		return EIO;

	/* The small image of the frosted glass. */
	error = blur_create(server, glass);
	if (error != 0)
		return error;

	/* The picture in both. */
	error = wallpaper_fill(server, glass, server->wallpaper_path);
	if (error != 0)
		return error;

	/* Succeeded: the wallpaper is drawn. */
	return 0;
}

/*
 * Draws a picture (a binary PPM, or the landscape drawn here when path is
 * NULL or cannot be read) into the wallpaper's image and its blurred
 * copy, which are mapped, the output's size, and kept for the look's life.
 *
 * The picture is made one output row at a time (ws035-p133): each row is
 * packed, copied into the image (which is only written, never read), and
 * added to the frosted glass's block averages, so no output-sized copy of
 * the picture is kept.  Both the start and a wallpaper chosen later
 * (zwl_glass_wallpaper) come here.
 */
static int
wallpaper_fill(
	struct zwl_server *server,
	struct zwl_glass *glass,
	const char *path)
{
	struct wallpaper_picture picture;
	uint32_t *columns;
	uint32_t *line;
	uint32_t *sums;
	uint32_t *row;
	float *small;
	uint32_t width;
	uint32_t height;
	uint32_t small_width;
	uint32_t small_height;
	uint32_t x;
	uint32_t y;
	uint32_t source_y;
	uint32_t made_y;
	uint64_t started;
	int error;

	/* The sizes: the output's, and the frosted glass's. */
	started = zwl_milliseconds();
	width = server->width;
	height = server->height;
	small_width = width / GLASS_BLUR_SCALE;
	small_height = height / GLASS_BLUR_SCALE;

	/* The picture given, when it can be read; otherwise the landscape drawn here. */
	memset(&picture, 0, sizeof(picture));
	if (path != NULL) {
		error = wallpaper_load(path, &picture);
		if (error != 0)
			printf("ZWL GLASS no wallpaper: path=%s errno=%d\n", path, error);
	}

	/* One output row, each output column's source column, a block row's sums, and the small image. */
	line = malloc((size_t)width * sizeof(*line));
	columns = malloc((size_t)width * sizeof(*columns));
	sums = calloc((size_t)small_width * 3U, sizeof(*sums));
	small = calloc((size_t)small_width * small_height * 3U, sizeof(*small));
	if (line == NULL || columns == NULL || sums == NULL || small == NULL) {
		free(line);
		free(columns);
		free(sums);
		free(small);
		free(picture.data);
		return ENOMEM;
	}

	/* Each output column takes the source column it falls on (the nearest pixel), as a byte offset. */
	if (picture.data != NULL) {
		for (x = 0; x < width; x++)
			columns[x] = (uint32_t)((uint64_t)x * picture.width / width) * 3U;
	}

	/*
	 * Each output row: made (a row that falls on the same source row as
	 * the one above is that row again), copied into the image, and added to
	 * the block of the frosted glass it lies in.
	 */
	made_y = UINT32_MAX;
	for (y = 0; y < height; y++) {
		if (picture.data != NULL) {
			/* The picture's row, unless the line already holds it. */
			source_y = (uint32_t)((uint64_t)y * picture.height / height);
			if (source_y != made_y)
				wallpaper_row(&picture, columns, source_y, line, width);
			made_y = source_y;
		} else {
			/* The landscape's row. */
			landscape_row(y, width, height, line);
		}

		/* Into the image. */
		row = (uint32_t *)((unsigned char *)glass->wallpaper.map + (size_t)y * glass->wallpaper.row_pitch);
		memcpy(row, line, (size_t)width * sizeof(*line));

		/* Rows below the last whole block are not in the frosted glass. */
		if (y >= small_height * GLASS_BLUR_SCALE)
			continue;

		/* Into its block; a block's last row makes that row of the small image. */
		blur_add(line, sums, small_width);
		if (y % GLASS_BLUR_SCALE == GLASS_BLUR_SCALE - 1U)
			blur_average(sums, &small[(size_t)(y / GLASS_BLUR_SCALE) * small_width * 3U], small_width);
	}

	/* The rows are made; the file and the working rows are no longer needed. */
	free(line);
	free(columns);
	free(sums);
	free(picture.data);
	printf("ZWL STARTUP step=wallpaper-picture ms=%llu\n", (unsigned long long)(zwl_milliseconds() - started));

	/* The frosted glass. */
	error = blur_fill(server, glass, small);
	free(small);
	if (error != 0)
		return error;

	/* Succeeded: both images hold the picture. */
	return 0;
}

/* Packs one output row from a source row of the picture, each column from its source column. */
static void
wallpaper_row(
	const struct wallpaper_picture *picture,
	const uint32_t *columns,
	uint32_t source_y,
	uint32_t *line,
	uint32_t width)
{
	const unsigned char *source;
	const unsigned char *pixel;
	uint32_t x;

	/* The source row's first byte. */
	source = picture->data + picture->pixels + (size_t)source_y * picture->width * 3U;

	/* Each pixel as opaque BGRA (A, R, G, B from the top byte), as pack_pixel would make it. */
	for (x = 0; x < width; x++) {
		pixel = source + columns[x];
		line[x] = 0xff000000U | ((uint32_t)pixel[0] << 16) | ((uint32_t)pixel[1] << 8) | (uint32_t)pixel[2];
	}
}

/* Packs one output row of the landscape drawn here. */
static void
landscape_row(
	uint32_t y,
	uint32_t width,
	uint32_t height,
	uint32_t *line)
{
	float rgb[3];
	uint32_t x;

	/* Each pixel colored, then packed. */
	for (x = 0; x < width; x++) {
		wallpaper_pixel(x, y, width, height, rgb);
		line[x] = pack_pixel(rgb);
	}
}

/*
 * Colors one pixel of the landscape in the Kei look (ws035-p108,
 * plan/ws035/kei-identity-design.md): a bright sky that pales towards the
 * horizon with a soft sun, three ranges of misty hills (pale blue far away,
 * young green near), a still lake that reflects them, soft green leaves at
 * the top left and along the foot with a few blurred white flowers, and the
 * whole a little whitened, as if seen through haze.
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
		{ 0.40f, 0.08f, 0.3f, 0.86f, 0.95f },
		{ 0.47f, 0.06f, 2.1f, 0.81f, 0.86f },
		{ 0.54f, 0.04f, 4.7f, 0.80f, 0.68f }
	};
	static const float reds[3] = { 0.78f, 0.68f, 0.64f };

	/* The blurred white flowers along the foot: where (fractions of the output) and how large. */
	static const float flowers[6][3] = {
		{ 0.06f, 0.92f, 0.030f },
		{ 0.15f, 0.97f, 0.025f },
		{ 0.24f, 0.90f, 0.020f },
		{ 0.33f, 0.98f, 0.022f },
		{ 0.80f, 0.95f, 0.024f },
		{ 0.93f, 0.90f, 0.028f }
	};
	float u;
	float v;
	float mirror;
	float top;
	float mist;
	float sun;
	float lake;
	float leaves;
	float bloom;
	float dx;
	float dy;
	unsigned index;

	/* The place, as fractions of the output. */
	u = (float)x / (float)width;
	v = (float)y / (float)height;
	lake = 0.62f;

	/* Under the lake's edge the landscape is seen in the water. */
	mirror = v;
	if (v > lake)
		mirror = 2.0f * lake - v;

	/* The sky: light blue above, nearly white at the horizon, a glow to the upper right. */
	rgb[0] = 0.76f + 0.22f * mirror;
	rgb[1] = 0.87f + 0.11f * mirror;
	rgb[2] = 0.98f + 0.01f * mirror;
	sun = expf(-((u - 0.78f) * (u - 0.78f) * 18.0f + (mirror - 0.16f) * (mirror - 0.16f) * 30.0f));
	rgb[0] += 0.12f * sun;
	rgb[1] += 0.09f * sun;
	rgb[2] += 0.03f * sun;

	/* Each range in front of the ones behind it, misty towards its foot. */
	for (index = 0; index < 3U; index++) {
		top = ridge(u, ranges[index][0], ranges[index][1], ranges[index][2]);
		if (mirror < top)
			continue;
		mist = (mirror - top) / 0.14f;
		if (mist > 1.0f)
			mist = 1.0f;
		rgb[0] = reds[index] + (0.93f - reds[index]) * mist * 0.55f;
		rgb[1] = ranges[index][3] + (0.96f - ranges[index][3]) * mist * 0.55f;
		rgb[2] = ranges[index][4] + (0.99f - ranges[index][4]) * mist * 0.55f;
	}

	/* The water is a little bluer than what it reflects, deeper towards the bottom. */
	if (v > lake) {
		rgb[0] = rgb[0] * 0.94f - (v - lake) * 0.10f;
		rgb[1] = rgb[1] * 0.97f - (v - lake) * 0.04f;
		rgb[2] = rgb[2] * 1.00f - (v - lake) * 0.02f;
		rgb[0] += 0.004f * sinf((float)y * 0.9f + u * 3.0f);
		rgb[1] += 0.004f * sinf((float)y * 0.9f + u * 3.0f);
	}

	/* Leaves: out of focus at the top left, and a meadow rising at both lower corners. */
	dx = u - 0.02f;
	dy = v - 0.04f;
	leaves = 0.75f * expf(-(dx * dx * 14.0f + dy * dy * 22.0f));
	top = 0.84f - 0.10f * (2.0f * fabsf(u - 0.5f));
	if (v > top) {
		mist = (v - top) / 0.16f;
		if (mist > 1.0f)
			mist = 1.0f;
		leaves += 0.85f * mist;
	}

	/* The leaves' colour over the scene, as much as they cover. */
	if (leaves > 1.0f)
		leaves = 1.0f;
	rgb[0] += (0.66f - rgb[0]) * leaves;
	rgb[1] += (0.82f - rgb[1]) * leaves;
	rgb[2] += (0.58f - rgb[2]) * leaves;

	/* The white flowers, blurred, among the leaves along the foot. */
	for (index = 0; index < 6U; index++) {
		dx = (u - flowers[index][0]) * (float)width / (float)height;
		dy = v - flowers[index][1];
		bloom = 0.85f * expf(-(dx * dx + dy * dy) / (flowers[index][2] * flowers[index][2]));
		rgb[0] += (0.98f - rgb[0]) * bloom;
		rgb[1] += (0.99f - rgb[1]) * bloom;
		rgb[2] += (0.97f - rgb[2]) * bloom;
	}

	/* The haze over everything: a fifth of the way to white. */
	for (index = 0; index < 3U; index++)
		rgb[index] += (1.0f - rgb[index]) * 0.20f;

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

/* Makes the frosted glass's small image, sampled linearly (blur_fill draws it). */
static int
blur_create(
	struct zwl_server *server,
	struct zwl_glass *glass)
{
	VkResult result;

	/* The output's size divided by GLASS_BLUR_SCALE. */
	result = zwl_host_image_create(server->compose, server->width / GLASS_BLUR_SCALE, server->height / GLASS_BLUR_SCALE, server->compose->linear_sampler, &glass->blurred);
	if (result != VK_SUCCESS)
		return EIO;

	/* Succeeded: the image is there. */
	return 0;
}

/*
 * Draws the frosted glass from its small image (the wallpaper averaged down
 * by GLASS_BLUR_SCALE, blur_average): blurred by repeated box passes (close
 * to a Gaussian) in place, then packed into the image.
 */
static int
blur_fill(
	struct zwl_server *server,
	struct zwl_glass *glass,
	float *small)
{
	uint32_t *row;
	float *scratch;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
	unsigned pass;

	/* The small image's size. */
	width = server->width / GLASS_BLUR_SCALE;
	height = server->height / GLASS_BLUR_SCALE;

	/* The passes' working copy. */
	scratch = calloc((size_t)width * height * 3U, sizeof(float));
	if (scratch == NULL)
		return ENOMEM;

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
	free(scratch);
	return 0;
}

/* Adds a packed output row to the sums of the blocks it crosses (three channels a block, red first). */
static void
blur_add(
	const uint32_t *line,
	uint32_t *sums,
	uint32_t small_width)
{
	uint32_t pixel;
	uint32_t x;
	uint32_t dx;

	/* Each block's GLASS_BLUR_SCALE pixels of this row. */
	for (x = 0; x < small_width; x++) {
		for (dx = 0; dx < GLASS_BLUR_SCALE; dx++) {
			pixel = line[x * GLASS_BLUR_SCALE + dx];
			sums[x * 3U] += (pixel >> 16) & 0xffU;
			sums[x * 3U + 1U] += (pixel >> 8) & 0xffU;
			sums[x * 3U + 2U] += pixel & 0xffU;
		}
	}
}

/* Makes one row of the small image from a block row's sums (0..1 each), and clears the sums for the next. */
static void
blur_average(
	uint32_t *sums,
	float *small,
	uint32_t small_width)
{
	uint32_t index;

	/* Each channel of each block: the average of its GLASS_BLUR_SCALE squared pixels. */
	for (index = 0; index < small_width * 3U; index++) {
		small[index] = (float)sums[index] / (255.0f * (float)(GLASS_BLUR_SCALE * GLASS_BLUR_SCALE));
		sums[index] = 0;
	}
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
	 * the compositor's start.
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
	uint64_t started;
	int error;

	/* The pen starts at the top left; each row is as tall as its tallest glyph. */
	started = zwl_milliseconds();
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
	printf("ZWL STARTUP step=glyphs-text ms=%llu\n", (unsigned long long)(zwl_milliseconds() - started));
	started = zwl_milliseconds();
	pen_y += line + 1U;
	error = atlas_icons(glass, &pen_y);
	if (error != 0)
		return error;
	printf("ZWL STARTUP step=glyphs-icons ms=%llu\n", (unsigned long long)(zwl_milliseconds() - started));
	started = zwl_milliseconds();

	/* The Kei mark's layers in the row after them. */
	error = atlas_mark(glass, &pen_y);
	if (error != 0)
		return error;
	printf("ZWL STARTUP step=glyphs-mark ms=%llu\n", (unsigned long long)(zwl_milliseconds() - started));

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
	struct stat status;
	unsigned char *data;
	ssize_t count;
	size_t capacity;
	size_t length;
	int descriptor;
	int error;

	/* The file. */
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return NULL;

	/* Its size, bounded by GLASS_FILE_MAX (ws035-p133: the buffer is as large as the file). */
	error = fstat(descriptor, &status);
	if (error != 0) {
		close(descriptor);
		return NULL;
	}

	/* A file larger than the bound is read to the bound. */
	capacity = GLASS_FILE_MAX;
	if (status.st_size >= 0 && (uint64_t)status.st_size < GLASS_FILE_MAX)
		capacity = (size_t)status.st_size;
	if (capacity == 0U) {
		close(descriptor);
		errno = EINVAL;
		return NULL;
	}

	/* Its bytes. */
	data = malloc(capacity);
	if (data == NULL) {
		close(descriptor);
		errno = ENOMEM;
		return NULL;
	}

	/* Read to the end or the bound: normally one read. */
	length = 0;
	for (;;) {
		count = read(descriptor, data + length, capacity - length);
		if (count <= 0)
			break;
		length += (size_t)count;
		if (length == capacity)
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
 * Reads a binary PPM (P6, maximum 255) for the wallpaper: its file's bytes
 * and header, kept in a picture that wallpaper_row scales from.  Returns 0,
 * or an errno value with nothing kept.
 */
static int
wallpaper_load(
	const char *path,
	struct wallpaper_picture *picture)
{
	unsigned char *data;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t maximum;
	size_t size;
	size_t at;
	int error;

	/* The file, read ahead when the prefetch read this path, otherwise now. */
	data = prefetch_take(path, &size);
	if (data == NULL)
		data = file_read(path, &size);
	if (data == NULL)
		return errno;

	/* The P6 magic. */
	if (size < 2U || data[0] != 'P' || data[1] != '6') {
		free(data);
		return EINVAL;
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
		return EINVAL;
	}

	/* One whitespace byte, then three bytes a pixel. */
	at++;
	if (at > size || (size - at) / 3U / source_width < source_height) {
		free(data);
		return EINVAL;
	}

	/* Succeeded: the picture is kept for its rows. */
	picture->data = data;
	picture->pixels = at;
	picture->width = source_width;
	picture->height = source_height;
	return 0;
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

/* Reads the wallpaper's file on the prefetch's thread; the result is taken after the join. */
static void *
prefetch_run(
	void *argument)
{
	(void)argument;

	/* The bytes, or NULL (the look then reads the file again and reports why). */
	glass_prefetch.data = file_read(glass_prefetch.path, &glass_prefetch.size);

	/* Succeeded: the thread ends; its result waits for the join. */
	return NULL;
}

/*
 * Takes the bytes the prefetch read, when it read this path: waits for its
 * thread first.  Returns NULL (and frees what it read for another path) when
 * the caller must read the file itself.
 */
static unsigned char *
prefetch_take(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	int same;

	/* No thread was started, or it was taken already. */
	if (!glass_prefetch.started)
		return NULL;

	/* The thread's end makes its result visible here. */
	(void)pthread_join(glass_prefetch.thread, NULL);
	glass_prefetch.started = 0;
	data = glass_prefetch.data;
	glass_prefetch.data = NULL;

	/* Bytes of another picture (the preferences chose one since) are not this one. */
	same = strcmp(path, glass_prefetch.path);
	if (same != 0) {
		free(data);
		return NULL;
	}

	/* Succeeded: the caller owns the bytes (NULL when the read failed). */
	*size = glass_prefetch.size;
	return data;
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
 * Draws an icon (GLASS_ICON_*) in a square of a size in pixels at (x, y):
 * a titlebar icon from the atlas's icon of that size, or of the nearest
 * one scaled; one of App Home's pictures from its one size, scaled.
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

	/* The larger size for anything above the smaller; App Home's pictures have their own. */
	size = 0;
	if (pixels > glass_icon_pixels[0])
		size = 1;
	glyph = &glass->icons[size][icon];
	if (icon >= GLASS_ICON_FIRST_APP)
		glyph = &glass->app_icons[icon - GLASS_ICON_FIRST_APP];
	if (icon >= GLASS_ICON_FIRST_APP && pixels <= GLASS_APP_ICON_SMALL * 3U / 2U)
		glyph = &glass->app_icons_small[icon - GLASS_ICON_FIRST_APP];

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

/*
 * Draws the Kei mark in a square of a size in pixels at (x, y): its seven
 * layers from the atlas, each in the colour of the look asked for, the
 * whole as opaque as asked (0..1).  A mark about the launcher's size uses
 * the layers rendered at that size (ws035-p117).
 */
void
glass_draw_mark(
	struct zwl_server *server,
	VkCommandBuffer command,
	int32_t x,
	int32_t y,
	unsigned pixels,
	enum glass_mark_look look,
	float opacity)
{
	/*
	 * The bar pale, its shade deeper, the leaf clearer and its shade the
	 * deep blue of the splash, the overlap deeper still, then the white
	 * light along the edges and the sheen (ws035-p109).  The panes are
	 * translucent, so the picture behind shows through as on the splash.
	 */
	static const float splash_colours[KEILAND_MARK_LAYERS][4] = {
		{ 0.663f, 0.765f, 0.965f, 0.69f },
		{ 0.498f, 0.635f, 0.941f, 0.35f },
		{ 0.639f, 0.847f, 0.980f, 0.67f },
		{ 0.227f, 0.525f, 0.961f, 0.67f },
		{ 0.184f, 0.486f, 0.953f, 0.78f },
		{ 1.0f, 1.0f, 1.0f, 0.67f },
		{ 1.0f, 1.0f, 1.0f, 0.24f }
	};

	/*
	 * The same layers for the system bar (ws035-p118, the 2026-09-28 user
	 * decision): every pane a slightly deeper blue and nearly opaque, and
	 * the white light and sheen fainter, so that the mark stands out on
	 * the bar's light glass instead of fading into it.
	 */
	static const float bar_colours[KEILAND_MARK_LAYERS][4] = {
		{ 0.455f, 0.600f, 0.925f, 0.92f },
		{ 0.290f, 0.451f, 0.878f, 0.55f },
		{ 0.400f, 0.690f, 0.945f, 0.90f },
		{ 0.145f, 0.408f, 0.890f, 0.90f },
		{ 0.106f, 0.349f, 0.839f, 0.96f },
		{ 1.0f, 1.0f, 1.0f, 0.50f },
		{ 1.0f, 1.0f, 1.0f, 0.18f }
	};
	const float (*colours)[4];
	struct glass_shape shape;
	const struct glass_glyph *layers;
	const struct glass_glyph *glyph;
	struct zwl_glass *glass;
	unsigned layer;

	/* Nothing without the atlas. */
	glass = server->compose->glass;
	if (!glass->text)
		return;

	/* The colours of the look asked for. */
	colours = splash_colours;
	if (look == GLASS_MARK_BAR)
		colours = bar_colours;

	/* The layers rendered nearest the size: the small ones up to half again their size. */
	layers = glass->mark;
	if (pixels <= GLASS_MARK_SMALL_PIXELS * 3U / 2U)
		layers = glass->mark_small;

	/* Each layer's cell of the atlas over the square, in order. */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
		glyph = &layers[layer];
		glass_shape_init(&shape, (float)x, (float)y, (float)pixels, (float)pixels);
		shape.mode = MODE_TEXT;
		shape.uv[0] = (float)glyph->x / (float)GLASS_ATLAS_WIDTH;
		shape.uv[1] = (float)glyph->y / (float)GLASS_ATLAS_HEIGHT;
		shape.uv[2] = (float)(glyph->x + glyph->width) / (float)GLASS_ATLAS_WIDTH;
		shape.uv[3] = (float)(glyph->y + glyph->height) / (float)GLASS_ATLAS_HEIGHT;
		memcpy(shape.color, colours[layer], sizeof(shape.color));
		shape.color[3] *= opacity;
		shape.set = glass->atlas.set;
		glass_shape_draw(server, command, &shape);
	}
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
 * Renders every titlebar icon at each of its sizes into the atlas from a
 * row on, then App Home's pictures in a row of their own, and moves the
 * row past them; returns 0, or ENOSPC when the atlas is full.
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

	/* The titlebar's icons side by side, as tall as the largest size. */
	pen_x = 0;
	tallest = glass_icon_pixels[GLASS_ICON_SIZES - 1U];
	for (size = 0; size < GLASS_ICON_SIZES; size++) {
		pixels = glass_icon_pixels[size];
		for (icon = 0; icon < GLASS_ICON_FIRST_APP; icon++) {
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

	/* App Home's pictures in the row after the icons. */
	*pen_y += tallest + 1U;
	if (*pen_y + GLASS_APP_ICON_PIXELS > GLASS_ATLAS_HEIGHT)
		return ENOSPC;
	if (GLASS_ICON_APPS * (GLASS_APP_ICON_PIXELS + 1U) > GLASS_ATLAS_WIDTH)
		return ENOSPC;

	/* Each picture's coverage, into the atlas, and its place. */
	pen_x = 0;
	for (icon = 0; icon < GLASS_ICON_APPS; icon++) {
		zwl_icon_raster(GLASS_ICON_FIRST_APP + icon, GLASS_APP_ICON_PIXELS, bitmap, GLASS_APP_ICON_PIXELS);
		atlas_put(glass, bitmap, pen_x, *pen_y, GLASS_APP_ICON_PIXELS, GLASS_APP_ICON_PIXELS);
		glyph = &glass->app_icons[icon];
		glyph->x = pen_x;
		glyph->y = *pen_y;
		glyph->width = GLASS_APP_ICON_PIXELS;
		glyph->height = GLASS_APP_ICON_PIXELS;
		glyph->left = 0;
		glyph->top = 0;
		glyph->advance = (int32_t)GLASS_APP_ICON_PIXELS;

		/* The pen moves past it. */
		pen_x += GLASS_APP_ICON_PIXELS + 1U;
	}

	/* The small pictures after them in the same row, when they fit. */
	if (pen_x + GLASS_ICON_APPS * (GLASS_APP_ICON_SMALL + 1U) > GLASS_ATLAS_WIDTH)
		return ENOSPC;

	/* Each small picture's coverage, into the atlas, and its place. */
	for (icon = 0; icon < GLASS_ICON_APPS; icon++) {
		zwl_icon_raster(GLASS_ICON_FIRST_APP + icon, GLASS_APP_ICON_SMALL, bitmap, GLASS_APP_ICON_SMALL);
		atlas_put(glass, bitmap, pen_x, *pen_y, GLASS_APP_ICON_SMALL, GLASS_APP_ICON_SMALL);
		glyph = &glass->app_icons_small[icon];
		glyph->x = pen_x;
		glyph->y = *pen_y;
		glyph->width = GLASS_APP_ICON_SMALL;
		glyph->height = GLASS_APP_ICON_SMALL;
		glyph->left = 0;
		glyph->top = 0;
		glyph->advance = (int32_t)GLASS_APP_ICON_SMALL;

		/* The pen moves past it. */
		pen_x += GLASS_APP_ICON_SMALL + 1U;
	}

	/* The row after the pictures. */
	*pen_y += GLASS_APP_ICON_PIXELS + 1U;

	/* Succeeded: the icons are in the atlas. */
	return 0;
}

/*
 * Renders the Kei mark's layers into the atlas side by side from a row on,
 * the small ones at the launcher's size in the column right of them
 * (ws035-p117), and moves the row past them; returns 0, or ENOSPC when the
 * atlas is full.
 */
static int
atlas_mark(
	struct zwl_glass *glass,
	uint32_t *pen_y)
{
	static uint8_t bitmap[GLASS_MARK_PIXELS * GLASS_MARK_PIXELS];
	struct glass_glyph *glyph;
	unsigned layer;
	uint32_t pen_x;
	uint32_t column_x;

	/* The atlas must hold the row. */
	if (*pen_y + GLASS_MARK_PIXELS > GLASS_ATLAS_HEIGHT)
		return ENOSPC;

	/* Each layer's coverage, into the atlas, and its place. */
	pen_x = 0;
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
		keiland_mark_raster(layer, GLASS_MARK_PIXELS, bitmap, GLASS_MARK_PIXELS);
		atlas_put(glass, bitmap, pen_x, *pen_y, GLASS_MARK_PIXELS, GLASS_MARK_PIXELS);
		glyph = &glass->mark[layer];
		glyph->x = pen_x;
		glyph->y = *pen_y;
		glyph->width = GLASS_MARK_PIXELS;
		glyph->height = GLASS_MARK_PIXELS;
		glyph->left = 0;
		glyph->top = 0;
		glyph->advance = (int32_t)GLASS_MARK_PIXELS;

		/* The pen moves past it. */
		pen_x += GLASS_MARK_PIXELS + 1U;
	}

	/* The small layers must fit the column left of the row's end. */
	column_x = pen_x;
	if (column_x + GLASS_MARK_SMALL_ACROSS * (GLASS_MARK_SMALL_PIXELS + 1U) > GLASS_ATLAS_WIDTH)
		return ENOSPC;

	/* Each small layer's coverage, into its cell of the column, and its place. */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
		keiland_mark_raster(layer, GLASS_MARK_SMALL_PIXELS, bitmap, GLASS_MARK_SMALL_PIXELS);
		glyph = &glass->mark_small[layer];
		glyph->x = column_x + (layer % GLASS_MARK_SMALL_ACROSS) * (GLASS_MARK_SMALL_PIXELS + 1U);
		glyph->y = *pen_y + (layer / GLASS_MARK_SMALL_ACROSS) * (GLASS_MARK_SMALL_PIXELS + 1U);
		glyph->width = GLASS_MARK_SMALL_PIXELS;
		glyph->height = GLASS_MARK_SMALL_PIXELS;
		glyph->left = 0;
		glyph->top = 0;
		glyph->advance = (int32_t)GLASS_MARK_SMALL_PIXELS;
		atlas_put(glass, bitmap, glyph->x, glyph->y, GLASS_MARK_SMALL_PIXELS, GLASS_MARK_SMALL_PIXELS);
	}

	/* The row after the mark. */
	*pen_y += GLASS_MARK_PIXELS + 1U;

	/* Succeeded: the mark is in the atlas. */
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
	const struct glass_glyph *glyph;
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

	/* No font has it yet: the colour emoji font, opened once on the first such character (ws102-p019). */
	if (id == 0U && glass->emoji_tried == 0U) {
		glass->emoji_tried = 1;
		glass->emoji_face = -1;
		error = glass_open_face(glass, GLASS_EMOJI_FONT);
		if (error == 0)
			glass->emoji_face = (int)glass->face_count - 1;
		printf("ZWL GLASS emoji font: path=%s errno=%d\n", GLASS_EMOJI_FONT, error);
	}

	/* The emoji font's colour glyph, when it has the character. */
	if (id == 0U && glass->emoji_tried != 0U && glass->emoji_face >= 0) {
		id = truetype_glyph_index(glass->faces[glass->emoji_face], codepoint);
		if (id != 0U) {
			glyph = glass_cache_color(glass, size, codepoint, id, oldest);
			return glyph;
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
	cell->glyph.color = 0;
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

/*
 * Draws a character's colour glyph of the emoji font into a cell of the
 * cache (its premultiplied colours as they are), for glass_draw_glyph_at
 * to draw as an image (ws102-p019); NULL when it cannot be drawn or is
 * larger than a cell.
 */
static const struct glass_glyph *
glass_cache_color(
	struct zwl_glass *glass,
	enum glass_size size,
	uint32_t codepoint,
	unsigned id,
	unsigned slot)
{
	struct keiland_color_image image;
	struct glass_cached *cell;
	uint32_t *row;
	unsigned per_row;
	int error;
	int line;

	/* The glyph's colours at the size, which must fit a cell. */
	error = keiland_color_glyph(glass->faces[glass->emoji_face], id, glass_pixels[size], &image);
	if (error != 0)
		return NULL;
	if (image.width > (int)GLASS_CELL || image.height > (int)GLASS_CELL) {
		free(image.pixels);
		return NULL;
	}

	/* The cell's place in the atlas, and the colours into it row by row. */
	cell = &glass->cache[slot];
	per_row = GLASS_ATLAS_WIDTH / GLASS_CELL;
	cell->glyph.x = (slot % per_row) * GLASS_CELL;
	cell->glyph.y = glass->cache_top + (slot / per_row) * GLASS_CELL;
	for (line = 0; line < image.height; line++) {
		row = (uint32_t *)((unsigned char *)glass->atlas.map + (size_t)(cell->glyph.y + (uint32_t)line) * glass->atlas.row_pitch);
		memcpy(row + cell->glyph.x, image.pixels + (size_t)line * (size_t)image.width, (size_t)image.width * sizeof(uint32_t));
	}

	/* The colours are in the atlas now. */
	free(image.pixels);

	/* Succeeded: the cell holds the character's colour glyph. */
	cell->codepoint = codepoint;
	cell->size = (unsigned)size;
	cell->glyph.width = (uint32_t)image.width;
	cell->glyph.height = (uint32_t)image.height;
	cell->glyph.left = image.left;
	cell->glyph.top = image.top;
	cell->glyph.advance = image.advance;
	cell->glyph.color = 1;
	glass->clock++;
	cell->used = glass->clock;
	printf("ZWL GLASS glyph codepoint=U+%04X size=%u face=emoji cell=%u color=1\n", codepoint, (unsigned)size, slot);
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

	/* The glyph's pixels: coverage in the colour, or a colour glyph as an image (ws102-p019). */
	glass_shape_init(&shape, (float)(x + glyph->left), (float)(baseline - glyph->top), (float)glyph->width, (float)glyph->height);
	shape.mode = MODE_TEXT;
	if (glyph->color)
		shape.mode = MODE_IMAGE;
	shape.uv[0] = (float)glyph->x / (float)GLASS_ATLAS_WIDTH;
	shape.uv[1] = (float)glyph->y / (float)GLASS_ATLAS_HEIGHT;
	shape.uv[2] = (float)(glyph->x + glyph->width) / (float)GLASS_ATLAS_WIDTH;
	shape.uv[3] = (float)(glyph->y + glyph->height) / (float)GLASS_ATLAS_HEIGHT;
	memcpy(shape.color, color, sizeof(shape.color));
	shape.set = glass->atlas.set;
	glass_shape_draw(server, command, &shape);
}
