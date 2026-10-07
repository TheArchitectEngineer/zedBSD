/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws179-p001: draws libkeiland's widgets that use the accent -- a main
 * button (at rest and under the pointer), a switch that is on, a slider and
 * a focused field -- in each of the eight accents, a row each, in the light
 * appearance and in the dark one, into two PPM pictures to look at.  It
 * checks that the main button's label is drawn in the accent's ink (a pixel
 * of the ink inside the button).  Last line: HOST-ACCENT-WIDGETS PASS.
 *
 *   host-accent-widgets SANS FALLBACK OUT   (OUT-light.ppm, OUT-dark.ppm)
 */

#include <keiland/keiland.h>

#include "userland/desktop/libkeiland/ui/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The picture's size, a row's height, and where the button is in a row. */
#define WIDGETS_WIDTH		720
#define WIDGETS_ROW		56
#define WIDGETS_HEIGHT		(WIDGETS_ROW * 8 + 16)
#define WIDGETS_BUTTON_X	16
#define WIDGETS_BUTTON_WIDTH	120

/* The pixels drawn into (the test's one thread alone). */
static uint32_t widgets_pixels[WIDGETS_WIDTH * WIDGETS_HEIGHT];

/* The appearance theme.c asks for (the one being drawn). */
static unsigned widgets_appearance;

static int widgets_draw(struct kl_text *text, unsigned appearance, const char *path);
static int widgets_write(const char *path);
static int widgets_has_ink(int x, int y, int width, int height, kl_color ink);

/* Draws both pictures and checks the labels' ink. */
int
main(
	int argc,
	char **argv)
{
	struct kl_text text;
	char path[512];
	int failures;
	int error;

	/* The fonts and the output's stem. */
	if (argc < 4) {
		fprintf(stderr, "usage: host-accent-widgets SANS FALLBACK OUT\n");
		return 2;
	}
	error = kl_text_open(&text, argv[1], argv[2]);
	if (error != 0) {
		fprintf(stderr, "host-accent-widgets: cannot open the fonts (%d)\n", error);
		return 1;
	}

	/* The light picture, then the dark one. */
	failures = 0;
	snprintf(path, sizeof(path), "%s-light.ppm", argv[3]);
	failures += widgets_draw(&text, KL_APPEARANCE_LIGHT, path);
	snprintf(path, sizeof(path), "%s-dark.ppm", argv[3]);
	failures += widgets_draw(&text, KL_APPEARANCE_DARK, path);

	/* The summary. */
	if (failures != 0) {
		printf("HOST-ACCENT-WIDGETS FAIL failures=%d\n", failures);
		return 1;
	}

	/* Succeeded: every label in its accent's ink. */
	printf("HOST-ACCENT-WIDGETS PASS\n");
	return 0;
}

/* Answers the appearance being drawn, for libkeiland's theme.c. */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	/* The picture's. */
	(void)appearance;
	return widgets_appearance;
}

/* Draws one appearance's eight rows into a picture; returns how many labels lack their ink. */
static int
widgets_draw(
	struct kl_text *text,
	unsigned appearance,
	const char *path)
{
	struct kl_canvas canvas;
	struct kl_style style;
	struct kl_field field;
	struct kl_rect rect;
	struct kl_ui *ui;
	const struct kl_theme *theme;
	unsigned accent;
	double value;
	int failures;
	int has;
	int on;
	int y;

	/* The canvas on the theme's ground. */
	widgets_appearance = appearance;
	(void)kl_canvas_init(&canvas, widgets_pixels, WIDGETS_WIDTH, WIDGETS_WIDTH, WIDGETS_HEIGHT);
	keiui_theme_set(appearance, KL_ACCENT_BLUE);
	theme = kl_theme_default();
	rect.x = 0;
	rect.y = 0;
	rect.width = WIDGETS_WIDTH;
	rect.height = WIDGETS_HEIGHT;
	kl_canvas_fill(&canvas, &rect, theme->panel);

	/* Each accent's row. */
	failures = 0;
	for (accent = 0; accent < KL_ACCENTS; accent++) {
		keiui_theme_set(appearance, accent);
		theme = kl_theme_default();
		ui = kl_ui_create();
		if (ui == NULL)
			return 1;
		memset(&style, 0, sizeof(style));
		style.canvas = &canvas;
		style.text = text;
		style.theme = theme;
		y = 8 + (int)accent * WIDGETS_ROW;

		/* The main button, a switch that is on, a slider at 60%, and a focused field. */
		kl_ui_begin(ui, 1000U);
		rect.x = WIDGETS_BUTTON_X;
		rect.y = y + 8;
		rect.width = WIDGETS_BUTTON_WIDTH;
		rect.height = theme->control_height;
		(void)kl_button(ui, &style, 1U, &rect, "Apply", KL_BUTTON_PRIMARY);
		on = 1;
		(void)kl_switch(ui, &style, 2U, 160, y + 12, &on, 0U);
		rect.x = 230;
		rect.width = 200;
		value = 60.0;
		(void)kl_slider(ui, &style, 3U, &rect, 0.0, 100.0, 1.0, &value);
		memset(&field, 0, sizeof(field));
		kl_field_set(&field, "Kei");
		kl_ui_set_focus(ui, 4U, 0U);
		rect.x = 460;
		rect.width = 240;
		(void)kl_field(ui, &style, 4U, &rect, &field, NULL);
		(void)kl_ui_end(ui, 1000U);
		kl_ui_destroy(ui);

		/* The label in the accent's ink. */
		has = widgets_has_ink(WIDGETS_BUTTON_X, y + 8, WIDGETS_BUTTON_WIDTH, theme->control_height, theme->accent_ink);
		if (!has) {
			printf("HOST-ACCENT-WIDGETS FAIL label ink accent=%u appearance=%u\n", accent, appearance);
			failures++;
		}
	}

	/* The picture. */
	if (widgets_write(path) != 0)
		failures++;

	/* How many fell short. */
	return failures;
}

/* Tells whether a rectangle of the picture has a pixel of the ink (within a few levels). */
static int
widgets_has_ink(
	int x,
	int y,
	int width,
	int height,
	kl_color ink)
{
	uint32_t pixel;
	int row;
	int column;
	int dr;
	int dg;
	int db;

	/* Each pixel. */
	for (row = y; row < y + height; row++) {
		for (column = x; column < x + width; column++) {
			pixel = widgets_pixels[row * WIDGETS_WIDTH + column];
			dr = (int)((pixel >> 16) & 0xffU) - (int)((ink >> 16) & 0xffU);
			dg = (int)((pixel >> 8) & 0xffU) - (int)((ink >> 8) & 0xffU);
			db = (int)(pixel & 0xffU) - (int)(ink & 0xffU);
			if (abs(dr) <= 6 && abs(dg) <= 6 && abs(db) <= 6)
				return 1;
		}
	}

	/* None. */
	return 0;
}

/* Writes the picture as a binary PPM. */
static int
widgets_write(
	const char *path)
{
	FILE *file;
	uint32_t pixel;
	unsigned char rgb[3];
	int index;

	/* The file and its header. */
	file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	fprintf(file, "P6\n%d %d\n255\n", WIDGETS_WIDTH, WIDGETS_HEIGHT);

	/* Each pixel's three channels. */
	for (index = 0; index < WIDGETS_WIDTH * WIDGETS_HEIGHT; index++) {
		pixel = widgets_pixels[index];
		rgb[0] = (unsigned char)((pixel >> 16) & 0xffU);
		rgb[1] = (unsigned char)((pixel >> 8) & 0xffU);
		rgb[2] = (unsigned char)(pixel & 0xffU);
		(void)fwrite(rgb, 1, sizeof(rgb), file);
	}

	/* Written. */
	fclose(file);
	return 0;
}
