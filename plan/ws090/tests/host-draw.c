/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host tests of libkeiland's drawing layer (ws090-p002): the same scene is
 * drawn with the file manager's canvas, text and icons (fm_, and Settings'
 * line pictures, se_) and with libkeiland (kui_), and the two frames must be
 * the same to the byte -- the library was moved from them unchanged, so an
 * application moved onto it draws the same pixels.  The frames are also
 * written as PPM for a person to look at.
 *
 *   host-draw FONT FALLBACK OUTPUT-PREFIX
 */

#include "../../../userland/desktop/settings/settings.h"

#include <keiui.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The frame's size. */
#define DRAW_WIDTH	640
#define DRAW_HEIGHT	480

/* How many of the file manager's icons and of Settings' line pictures there are. */
#define DRAW_FM_ICONS	((int)FM_ICON_DOWN + 1)
#define DRAW_SE_GLYPHS	((int)SE_GLYPH_CHEVRON + 1)

/* The tests run and failed. */
static int test_count;
static int test_failed;

static void check(int condition, const char *what);
static void scene_fm(struct fm_canvas *canvas, struct fm_text *text);
static void scene_kui(struct kui_canvas *canvas, struct kui_text *text);
static void write_ppm(const char *path, const uint32_t *pixels);

/*
 * Runs the tests.
 */
int
main(
	int argc,
	char **argv)
{
	static uint32_t fm_pixels[DRAW_WIDTH * DRAW_HEIGHT];
	static uint32_t kui_pixels[DRAW_WIDTH * DRAW_HEIGHT];
	struct fm_canvas fm_canvas;
	struct kui_canvas kui_canvas;
	struct fm_text fm_text;
	struct kui_text kui_text;
	const struct kui_theme *theme;
	char path[1024];
	size_t differ;
	size_t index;
	int error;

	/* The fonts and where pictures go. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-draw FONT FALLBACK OUTPUT-PREFIX\n");
		return 2;
	}

	/* Both implementations' fonts. */
	error = fm_text_open(&fm_text, argv[1], argv[2]);
	check(error == 0, "fm text opens");
	error = kui_text_open(&kui_text, argv[1], argv[2]);
	check(error == 0, "kui text opens");
	if (test_failed != 0)
		return 1;

	/* Both canvases over cleared pixels. */
	error = fm_canvas_init(&fm_canvas, fm_pixels, DRAW_WIDTH, DRAW_WIDTH, DRAW_HEIGHT);
	check(error == 0, "fm canvas");
	error = kui_canvas_init(&kui_canvas, kui_pixels, DRAW_WIDTH, DRAW_WIDTH, DRAW_HEIGHT);
	check(error == 0, "kui canvas");

	/* The same scene with each. */
	scene_fm(&fm_canvas, &fm_text);
	scene_kui(&kui_canvas, &kui_text);

	/* The frames must be the same to the byte. */
	differ = 0;
	for (index = 0; index < (size_t)DRAW_WIDTH * DRAW_HEIGHT; index++) {
		if (fm_pixels[index] != kui_pixels[index])
			differ++;
	}

	/* How many differ, and none may. */
	printf("pixels that differ: %zu\n", differ);
	check(differ == 0, "libkeiland draws the scene as Files and Settings do");

	/* Something was drawn at all (a blank frame would also match). */
	differ = 0;
	for (index = 0; index < (size_t)DRAW_WIDTH * DRAW_HEIGHT; index++) {
		if (kui_pixels[index] != 0U)
			differ++;
	}

	/* More than half the frame is drawn on. */
	check(differ > (size_t)DRAW_WIDTH * DRAW_HEIGHT / 2U, "the scene covers the frame");

	/* The theme holds Files' values (userland/desktop/files/files.h, FM_COLOR_*). */
	theme = kui_theme_default();
	check(theme->accent == FM_RGB(0x2f7cf6), "theme accent is Files'");
	check(theme->text == FM_RGB(0x1e2632), "theme text is Files'");
	check(theme->hover == FM_RGBA(0x5a6b85, 18), "theme hover is Files'");
	check(theme->glass_content == FM_RGBA(0xffffff, 60), "theme glass content is Files'");
	check(theme->row_height == 28, "theme row height is Files' list row");

	/* The colour mixing of both. */
	check(kui_color_mix(KUI_RGB(0x000000), KUI_RGB(0xffffff), 0.5f) == fm_color_mix(FM_RGB(0x000000), FM_RGB(0xffffff), 0.5f), "color mix");

	/* The pictures. */
	snprintf(path, sizeof(path), "%s-kui.ppm", argv[3]);
	write_ppm(path, kui_pixels);
	snprintf(path, sizeof(path), "%s-fm.ppm", argv[3]);
	write_ppm(path, fm_pixels);

	/* The fonts and canvases go. */
	fm_canvas_release(&fm_canvas);
	kui_canvas_release(&kui_canvas);
	fm_text_close(&fm_text);
	kui_text_close(&kui_text);

	/* The outcome. */
	printf("host-draw: %d/%d passed\n", test_count - test_failed, test_count);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Records one check. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted, and a failure said. */
	test_count++;
	if (condition)
		return;
	test_failed++;
	printf("FAIL: %s\n", what);
}

/* Draws the scene with the file manager's and Settings' drawing. */
static void
scene_fm(
	struct fm_canvas *canvas,
	struct fm_text *text)
{
	static const float star[] = { 520, 20, 540, 70, 590, 70, 550, 100, 565, 150, 520, 120, 475, 150, 490, 100, 450, 70, 500, 70 };
	struct fm_image image;
	struct fm_image scaled;
	struct fm_rect rect;
	int index;
	int made;
	int x;
	int y;

	/* The ground and cards. */
	fm_canvas_clear(canvas);
	rect.x = 0;
	rect.y = 0;
	rect.width = DRAW_WIDTH;
	rect.height = DRAW_HEIGHT;
	fm_canvas_gradient(canvas, &rect, FM_RGB(0xeef2f7), FM_RGB(0xe6ebf3));
	fm_canvas_shadow(canvas, 20.0f, 20.0f, 300.0f, 160.0f, 16.0f, 12.0f, FM_RGBA(0x1f3a66, 34));
	fm_canvas_round(canvas, 20.0f, 20.0f, 300.0f, 160.0f, 16.0f, FM_RGB(0xffffff));
	fm_canvas_round_border(canvas, 20.0f, 20.0f, 300.0f, 160.0f, 16.0f, 1.0f, FM_RGB(0xe2e7ef));
	fm_canvas_round_gradient(canvas, 340.0f, 20.0f, 100.0f, 60.0f, 12.0f, FM_RGB(0x5aa2f5), FM_RGB(0x2f7cf6));

	/* Circles, a ring, a star and lines. */
	fm_canvas_circle(canvas, 390.0f, 130.0f, 30.0f, FM_RGBA(0xe5484d, 200));
	fm_canvas_ring(canvas, 390.0f, 130.0f, 40.0f, 4.0f, 0.7f, FM_RGB(0x2f7cf6));
	fm_canvas_polygon(canvas, star, 10, FM_RGBA(0xf5a524, 220));
	fm_canvas_line(canvas, 40.0f, 200.0f, 600.0f, 230.0f, 2.5f, FM_RGB(0x46526a));

	/* Text: plain, bold, cut to a width, Japanese through the fallback, and a line break. */
	(void)fm_text_draw(text, canvas, 40, 60, "Kei widgets", 11, 20U, 1, FM_RGB(0x1e2632));
	(void)fm_text_draw_fit(text, canvas, 40, 90, "A long name that does not fit its column at all", 13U, 0, 260, FM_RGB(0x6b7585));
	(void)fm_text_draw(text, canvas, 40, 120, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe6\x96\x87\xe5\xad\x97", 18, 16U, 0, FM_RGB(0x1e2632));
	x = (int)fm_text_break(text, "one two three four five six seven", 13U, 0, 120);
	(void)fm_text_draw(text, canvas, 40, 150, "one two three four five six seven", (size_t)x, 13U, 0, FM_RGB(0xa3abb8));

	/* Every icon of the file manager, then every line picture of Settings. */
	for (index = 0; index < DRAW_FM_ICONS; index++)
		fm_icon_draw(canvas, (enum fm_icon)index, 20.0f + (float)(index % 12) * 50.0f, 250.0f + (float)(index / 12) * 50.0f, 32.0f, FM_RGB(0x46526a));
	for (index = 0; index < DRAW_SE_GLYPHS; index++)
		se_glyph_draw(canvas, (unsigned)index, 20.0f + (float)(index % 13) * 46.0f, 360.0f + (float)(index / 13) * 46.0f, 30.0f, FM_RGB(0x2f7cf6));

	/* A folder, and a file with its band and label (Files has no tags since ws127-p012). */
	fm_icon_folder(canvas, 460.0f, 170.0f, 48.0f, FM_RGB(0x5aa2f5));
	fm_icon_file(canvas, text, 520.0f, 170.0f, 48.0f, FM_RGB(0xe5484d), "PDF");

	/* A small picture of a colour ramp. */
	made = fm_image_create(&image, 16, 16);
	if (made != 0)
		return;
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++)
			image.pixels[y * 16 + x] = 0xff000000U | (uint32_t)(x * 16) << 16 | (uint32_t)(y * 16) << 8 | 0x80U;
	}

	/* Scaled up and drawn with rounded corners. */
	made = fm_image_create(&scaled, 40, 40);
	if (made == 0) {
		fm_image_scale(&image, &scaled);
		fm_canvas_image(canvas, &scaled, 580.0f, 400.0f, 40.0f, 40.0f, 8.0f, 0.9f);
		fm_image_release(&scaled);
	}

	/* The small picture goes. */
	fm_image_release(&image);
}

/* Draws the same scene with libkeiland. */
static void
scene_kui(
	struct kui_canvas *canvas,
	struct kui_text *text)
{
	static const float star[] = { 520, 20, 540, 70, 590, 70, 550, 100, 565, 150, 520, 120, 475, 150, 490, 100, 450, 70, 500, 70 };
	struct kui_image image;
	struct kui_image scaled;
	struct kui_rect rect;
	int index;
	int made;
	int x;
	int y;

	/* The ground and cards. */
	kui_canvas_clear(canvas);
	rect.x = 0;
	rect.y = 0;
	rect.width = DRAW_WIDTH;
	rect.height = DRAW_HEIGHT;
	kui_canvas_gradient(canvas, &rect, KUI_RGB(0xeef2f7), KUI_RGB(0xe6ebf3));
	kui_canvas_shadow(canvas, 20.0f, 20.0f, 300.0f, 160.0f, 16.0f, 12.0f, KUI_RGBA(0x1f3a66, 34));
	kui_canvas_round(canvas, 20.0f, 20.0f, 300.0f, 160.0f, 16.0f, KUI_RGB(0xffffff));
	kui_canvas_round_border(canvas, 20.0f, 20.0f, 300.0f, 160.0f, 16.0f, 1.0f, KUI_RGB(0xe2e7ef));
	kui_canvas_round_gradient(canvas, 340.0f, 20.0f, 100.0f, 60.0f, 12.0f, KUI_RGB(0x5aa2f5), KUI_RGB(0x2f7cf6));

	/* Circles, a ring, a star and lines. */
	kui_canvas_circle(canvas, 390.0f, 130.0f, 30.0f, KUI_RGBA(0xe5484d, 200));
	kui_canvas_ring(canvas, 390.0f, 130.0f, 40.0f, 4.0f, 0.7f, KUI_RGB(0x2f7cf6));
	kui_canvas_polygon(canvas, star, 10, KUI_RGBA(0xf5a524, 220));
	kui_canvas_line(canvas, 40.0f, 200.0f, 600.0f, 230.0f, 2.5f, KUI_RGB(0x46526a));

	/* Text: plain, bold, cut to a width, Japanese through the fallback, and a line break. */
	(void)kui_text_draw(text, canvas, 40, 60, "Kei widgets", 11, 20U, 1, KUI_RGB(0x1e2632));
	(void)kui_text_draw_fit(text, canvas, 40, 90, "A long name that does not fit its column at all", 13U, 0, 260, KUI_RGB(0x6b7585));
	(void)kui_text_draw(text, canvas, 40, 120, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe6\x96\x87\xe5\xad\x97", 18, 16U, 0, KUI_RGB(0x1e2632));
	x = (int)kui_text_break(text, "one two three four five six seven", 13U, 0, 120);
	(void)kui_text_draw(text, canvas, 40, 150, "one two three four five six seven", (size_t)x, 13U, 0, KUI_RGB(0xa3abb8));

	/* Every icon of the file manager, then every line picture (KUI_ICON_TILES on). */
	for (index = 0; index < DRAW_FM_ICONS; index++)
		kui_icon_draw(canvas, (enum kui_icon)index, 20.0f + (float)(index % 12) * 50.0f, 250.0f + (float)(index / 12) * 50.0f, 32.0f, KUI_RGB(0x46526a));
	for (index = 0; index < DRAW_SE_GLYPHS; index++)
		kui_icon_draw(canvas, (enum kui_icon)((int)KUI_ICON_TILES + index), 20.0f + (float)(index % 13) * 46.0f, 360.0f + (float)(index / 13) * 46.0f, 30.0f, KUI_RGB(0x2f7cf6));

	/* A folder, and a file with its band and label (Files has no tags since ws127-p012). */
	kui_icon_folder(canvas, 460.0f, 170.0f, 48.0f, KUI_RGB(0x5aa2f5));
	kui_icon_file(canvas, text, 520.0f, 170.0f, 48.0f, KUI_RGB(0xe5484d), "PDF");

	/* A small picture of a colour ramp. */
	made = kui_image_create(&image, 16, 16);
	if (made != 0)
		return;
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++)
			image.pixels[y * 16 + x] = 0xff000000U | (uint32_t)(x * 16) << 16 | (uint32_t)(y * 16) << 8 | 0x80U;
	}

	/* Scaled up and drawn with rounded corners. */
	made = kui_image_create(&scaled, 40, 40);
	if (made == 0) {
		kui_image_scale(&image, &scaled);
		kui_canvas_image(canvas, &scaled, 580.0f, 400.0f, 40.0f, 40.0f, 8.0f, 0.9f);
		kui_image_release(&scaled);
	}

	/* The small picture goes. */
	kui_image_release(&image);
}

/* Writes premultiplied pixels over white as a PPM. */
static void
write_ppm(
	const char *path,
	const uint32_t *pixels)
{
	uint32_t pixel;
	unsigned alpha;
	unsigned channel;
	unsigned value;
	FILE *file;
	size_t index;

	/* The file. */
	file = fopen(path, "wb");
	if (file == NULL)
		return;

	/* Each pixel over white. */
	fprintf(file, "P6\n%d %d\n255\n", DRAW_WIDTH, DRAW_HEIGHT);
	for (index = 0; index < (size_t)DRAW_WIDTH * DRAW_HEIGHT; index++) {
		pixel = pixels[index];
		alpha = pixel >> 24;
		for (channel = 0; channel < 3U; channel++) {
			value = (pixel >> (16U - channel * 8U)) & 0xffU;
			value += 255U - alpha;
			if (value > 255U)
				value = 255U;
			fputc((int)value, file);
		}
	}

	/* The picture is written. */
	fclose(file);
}
