/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws091-p002: Image Viewer's parts that know neither Wayland nor Vulkan,
 * on the host (built with image.c, folder.c, view.c and chooser.c, and the
 * compat libraries' sources, by run-host.sh).
 *
 *   host-imageview unit                 the folder's order, the file names, the EXIF orientation
 *   host-imageview decode FILE OUT      level 0 of FILE as RGBA bytes in OUT; prints "W H LEVELS FRAMES FILE_W FILE_H ERROR"
 *   host-imageview frames FILE PREFIX   each composed frame of FILE as PREFIX-N (RGBA bytes); prints the delays
 *   host-imageview view DIR             the view on the images of DIR: the fit, the zoom about a point, the turns,
 *                                       the quad, the next and the previous image, a swipe
 *
 * Every check prints "ok NAME" or "FAIL NAME detail"; the exit status is 1
 * when one failed.
 */

#include "../../../userland/desktop/imageview/imageview.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int condition, const char *name, const char *format, ...);
static int unit(void);
static int decode(const char *path, const char *output);
static int frames(const char *path, const char *prefix);
static int view(const char *directory);
static void write_rgba(const char *path, const uint32_t *pixels, int width, int height);
static void exif_block(unsigned char *block, size_t *size, int big_endian, unsigned orientation);

/* Runs one of the tests. */
int
main(
	int argc,
	char **argv)
{
	int status;

	/* The test asked for. */
	status = 2;
	if (argc == 2 && strcmp(argv[1], "unit") == 0)
		status = unit();
	else if (argc == 4 && strcmp(argv[1], "decode") == 0)
		status = decode(argv[2], argv[3]);
	else if (argc == 4 && strcmp(argv[1], "frames") == 0)
		status = frames(argv[2], argv[3]);
	else if (argc == 3 && strcmp(argv[1], "view") == 0)
		status = view(argv[2]);
	else
		fprintf(stderr, "usage: host-imageview unit | decode FILE OUT | frames FILE PREFIX | view DIR\n");

	/* A failed check fails the run. */
	if (status == 0 && failures != 0)
		status = 1;
	return status;
}

/* Prints a check's outcome. */
static void
check(
	int condition,
	const char *name,
	const char *format,
	...)
{
	va_list arguments;

	/* A passed check. */
	if (condition) {
		printf("ok %s\n", name);
		return;
	}

	/* A failed one, with its detail. */
	failures++;
	printf("FAIL %s ", name);
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* The folder's order, the names shown, and the EXIF orientation. */
static int
unit(void)
{
	static const char *const ordered[] = {
		"a.png", "B.png", "IMG_2.jpg", "IMG_02b.jpg", "IMG_10.jpg", "img_11.JPG", "IMG_100.jpg", "photo 9.gif", "photo 10.gif", "z.png"
	};
	unsigned char block[64];
	size_t size;
	size_t index;
	unsigned value;
	int order;
	int result;

	/* Each name comes before the next. */
	for (index = 0; index + 1 < sizeof(ordered) / sizeof(ordered[0]); index++) {
		order = iv_folder_compare(ordered[index], ordered[index + 1]);
		check(order < 0, "order", "%s before %s: %d", ordered[index], ordered[index + 1], order);
		order = iv_folder_compare(ordered[index + 1], ordered[index]);
		check(order > 0, "order-reverse", "%s after %s: %d", ordered[index + 1], ordered[index], order);
	}
	check(iv_folder_compare("x.png", "x.png") == 0, "order-equal", "");
	check(iv_folder_compare("IMG_007.png", "IMG_7.png") != 0, "order-zeros-differ", "");

	/* The names shown and not. */
	check(iv_image_is_name("a.PNG") == 1, "name-png", "");
	check(iv_image_is_name("b.jpeg") == 1, "name-jpeg", "");
	check(iv_image_is_name("c.Jpe") == 1, "name-jpe", "");
	check(iv_image_is_name("d.gif") == 1, "name-gif", "");
	check(iv_image_is_name("e.pdf") == 0, "name-pdf", "");
	check(iv_image_is_name(".png") == 0, "name-hidden", "");
	check(iv_image_is_name("png") == 0, "name-bare", "");

	/* Each orientation in each byte order, and blocks that say nothing. */
	for (value = 1; value <= 8; value++) {
		exif_block(block, &size, 0, value);
		result = iv_image_orientation(block, size);
		check(result == (int)value, "exif-ii", "%u read as %d", value, result);
		exif_block(block, &size, 1, value);
		result = iv_image_orientation(block, size);
		check(result == (int)value, "exif-mm", "%u read as %d", value, result);
	}
	exif_block(block, &size, 0, 6);
	check(iv_image_orientation(block, 10) == 1, "exif-short", "");
	block[0] = 'X';
	check(iv_image_orientation(block, size) == 1, "exif-not", "");
	exif_block(block, &size, 0, 9);
	check(iv_image_orientation(block, size) == 1, "exif-bad-value", "");

	/* Succeeded. */
	return 0;
}

/* Decodes a file and writes its level 0. */
static int
decode(
	const char *path,
	const char *output)
{
	struct iv_image image;
	size_t level;
	int error;

	/* The image, as the viewer decodes it (no side limit). */
	error = iv_image_load(&image, path, 0);
	printf("%d %d %lu %lu %d %d %d\n", image.width, image.height, (unsigned long)image.level_count, (unsigned long)image.frame_count,
	    image.file_width, image.file_height, error);

	/* Its levels are halves of each other. */
	for (level = 1; level < image.level_count; level++) {
		check(image.levels[level].width == (image.levels[level - 1].width + 1) / 2 &&
		    image.levels[level].height == (image.levels[level - 1].height + 1) / 2, "level-halves", "level %lu", (unsigned long)level);
	}

	/* Level 0's pixels. */
	if (error == 0)
		write_rgba(output, image.levels[0].pixels, image.width, image.height);
	iv_image_free(&image);

	/* Succeeded: the caller compares. */
	return 0;
}

/* Decodes an animated file and writes each composed frame. */
static int
frames(
	const char *path,
	const char *prefix)
{
	char name[1024];
	struct iv_image image;
	size_t index;
	int error;

	/* The image and its frames. */
	error = iv_image_load(&image, path, 0);
	printf("%d %d %lu %d\n", image.width, image.height, (unsigned long)image.frame_count, error);

	/* Each frame and its delay. */
	for (index = 0; index < image.frame_count; index++) {
		snprintf(name, sizeof(name), "%s-%lu", prefix, (unsigned long)index);
		write_rgba(name, image.frames[index], image.width, image.height);
		printf("delay %lu %u\n", (unsigned long)index, image.delays[index]);
	}
	iv_image_free(&image);

	/* Succeeded: the caller compares. */
	return 0;
}

/*
 * The view over the images of a folder (made by run-host.sh: a.png
 * 4000x3000, b.png 400x300, c.png 300x400, in that order).
 */
static int
view(
	const char *directory)
{
	char path[1024];
	struct iv_app app;
	struct iv_place before;
	struct iv_place after;
	struct iv_quad quad;
	struct iv_event event;
	double fit;
	int error;

	/* The viewer in a 1000 x 720 window on the first image. */
	iv_app_init(&app, NULL, 1000, 720);
	snprintf(path, sizeof(path), "%s/a.png", directory);
	error = iv_app_open(&app, path);
	check(error == 0 && app.has_image, "open", "error %d", error);
	check(app.folder.count == 3 && app.folder.index == 0, "folder", "count %lu index %lu", (unsigned long)app.folder.count,
	    (unsigned long)app.folder.index);

	/* Fitted: the area is the window less the margins; 4000 x 3000 fits by its width. */
	check(app.area_width == 1000 - 2 * IV_MARGIN && app.area_height == 720 - 2 * IV_MARGIN, "area", "%d x %d", app.area_width, app.area_height);
	fit = (double)app.area_width / 4000.0;
	if ((double)app.area_height / 3000.0 < fit)
		fit = (double)app.area_height / 3000.0;
	check(fabs(app.scale - fit) < 1e-9 && app.fit, "fit", "scale %f expected %f", app.scale, fit);

	/* The quad is the content centred in the area, upright, from a level no smaller than shown. */
	iv_app_quad(&app, &quad);
	check(quad.visible && fabs(quad.x[1] - quad.x[0] - 4000.0 * fit) < 0.01 && fabs(quad.y[2] - quad.y[0] - 3000.0 * fit) < 0.01, "quad-size", "");
	check(fabs((quad.x[0] + quad.x[3]) / 2.0 - 500.0) < 0.01 && fabs((quad.y[0] + quad.y[3]) / 2.0 - 360.0) < 0.01, "quad-centred", "");
	check(app.current->levels[quad.level].width >= (int)floor(4000.0 * fit), "quad-level", "level %lu", (unsigned long)quad.level);

	/* A zoom about a point keeps the place under it. */
	iv_app_place_at(&app, 300.0, 200.0, &before);
	iv_app_zoom_at(&app, 1.0, 300.0, 200.0, 0);
	iv_app_place_at(&app, 300.0, 200.0, &after);
	check(fabs(before.x - after.x) < 0.01 && fabs(before.y - after.y) < 0.01 && !app.fit, "zoom-about", "%f,%f vs %f,%f", before.x, before.y,
	    after.x, after.y);
	iv_app_quad(&app, &quad);
	check(quad.level == 0 && !quad.nearest, "zoom-level", "level %lu", (unsigned long)quad.level);

	/* A turn right puts the image's top left corner at the top right. */
	iv_app_action(&app, IV_ACTION_FIT);
	app.now += IV_ANIMATION_MS + 1U;
	(void)iv_app_tick(&app, app.now);
	iv_app_action(&app, IV_ACTION_ROTATE_RIGHT);
	iv_app_quad(&app, &quad);
	check(app.rotation == 1 && quad.x[0] > quad.x[2] && quad.y[0] < quad.y[1], "turn-right", "rotation %u", app.rotation);
	check(fabs(app.scale - iv_app_fit_scale(&app)) < 1e-9, "turn-fit", "");
	iv_app_action(&app, IV_ACTION_ROTATE_LEFT);
	check(app.rotation == 0, "turn-back", "rotation %u", app.rotation);

	/* The next image: 400 x 300 is shown at 1:1 (the fit never enlarges). */
	iv_app_action(&app, IV_ACTION_NEXT);
	check(app.folder.index == 1 && app.current->width == 400 && fabs(app.scale - 1.0) < 1e-9, "next", "index %lu scale %f",
	    (unsigned long)app.folder.index, app.scale);

	/* The neighbours are decoded ahead, then kept when stepping. */
	while (iv_app_prefetch(&app))
		;
	check(app.next != NULL && app.previous != NULL, "prefetch", "");

	/* A swipe to the left far enough brings the next image in. */
	app.swipe = -500.0;
	iv_app_swipe_end(&app, 0.0, 1);
	check(app.folder.index == 2 && app.sliding, "swipe", "index %lu", (unsigned long)app.folder.index);

	/* A short swipe slides back. */
	app.now += 1000U;
	(void)iv_app_tick(&app, app.now);
	app.swipe = 50.0;
	iv_app_swipe_end(&app, 0.0, 1);
	check(app.folder.index == 2 && app.sliding && app.slide_from == 50.0, "swipe-back", "index %lu", (unsigned long)app.folder.index);

	/* Past the last image nothing changes; Home goes to the first. */
	iv_app_action(&app, IV_ACTION_NEXT);
	check(app.folder.index == 2, "last", "index %lu", (unsigned long)app.folder.index);
	memset(&event, 0, sizeof(event));
	event.type = IV_EVENT_KEY;
	event.key = IV_KEY_HOME;
	event.pressed = 1;
	iv_app_event(&app, &event);
	check(app.folder.index == 0, "home", "index %lu", (unsigned long)app.folder.index);

	/* The wheel zooms about the pointer. */
	memset(&event, 0, sizeof(event));
	event.type = IV_EVENT_AXIS;
	event.x = 700;
	event.y = 400;
	event.scroll = -60;
	iv_app_place_at(&app, 700.0, 400.0, &before);
	iv_app_event(&app, &event);
	iv_app_place_at(&app, 700.0, 400.0, &after);
	check(!app.fit && fabs(before.x - after.x) < 0.01 && fabs(before.y - after.y) < 0.01, "wheel", "scale %f", app.scale);

	/* Closing shows nothing. */
	iv_app_action(&app, IV_ACTION_CLOSE);
	iv_app_quad(&app, &quad);
	check(!app.has_image && !quad.visible, "close", "");
	iv_app_release(&app);

	/* Succeeded. */
	return 0;
}

/* Writes premultiplied opaque pixels as RGBA bytes. */
static void
write_rgba(
	const char *path,
	const uint32_t *pixels,
	int width,
	int height)
{
	unsigned char rgba[4];
	size_t index;
	FILE *file;

	/* Each pixel, red first. */
	file = fopen(path, "wb");
	if (file == NULL)
		return;
	for (index = 0; index < (size_t)width * (size_t)height; index++) {
		rgba[0] = (unsigned char)(pixels[index] >> 16);
		rgba[1] = (unsigned char)(pixels[index] >> 8);
		rgba[2] = (unsigned char)pixels[index];
		rgba[3] = (unsigned char)(pixels[index] >> 24);
		fwrite(rgba, 1U, 4U, file);
	}
	fclose(file);
}

/* Makes an EXIF block with one directory entry: the orientation. */
static void
exif_block(
	unsigned char *block,
	size_t *size,
	int big_endian,
	unsigned orientation)
{
	unsigned char *tiff;

	/* "Exif" and two zeros, then the TIFF header, the directory's count and its entry. */
	memset(block, 0, 64U);
	memcpy(block, "Exif\0\0", 6U);
	tiff = block + 6;
	if (big_endian) {
		memcpy(tiff, "MM\0*", 4U);
		tiff[7] = 8;
		tiff[9] = 1;
		tiff[10] = 0x01;
		tiff[11] = 0x12;
		tiff[13] = 3;
		tiff[17] = 1;
		tiff[19] = (unsigned char)orientation;
	} else {
		memcpy(tiff, "II*\0", 4U);
		tiff[4] = 8;
		tiff[8] = 1;
		tiff[10] = 0x12;
		tiff[11] = 0x01;
		tiff[12] = 3;
		tiff[14] = 1;
		tiff[18] = (unsigned char)orientation;
	}
	*size = 6U + 8U + 2U + 12U + 4U;
}
