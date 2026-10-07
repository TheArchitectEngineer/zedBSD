/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws179-p001: the host test of the accent colours (artwork/accent.h,
 * plan/ws179/design.md section 9.1).  For every accent in both appearances:
 *
 *   (1) the ink on the accent, at rest and with the main button's shade
 *       under the pointer (8% away from the ink), 4.6 or more;
 *   (2) the accent on the window's grounds (the ground's three colours, a
 *       card and a control laid over the ground), 3.0 or more;
 *   (3) the accent as text on those grounds and on the selection laid over
 *       them, 4.6 or more.
 *
 * Blue (the default) keeps the desktop's colours of before and is the
 * recorded exception of (1) and (3) (design.md section 10); its values are
 * checked to be those colours instead.  The theme made with an accent
 * (keiui_theme_with), Settings' and Files' palettes take the accent's
 * colours, and an index past the table is blue.  Last line: HOST-ACCENT
 * PASS.
 */

#include <keiland/keiland.h>

#include "userland/desktop/libkeiland/ui/internal.h"
#include "userland/desktop/settings/settings.h"
#include "userland/desktop/files/files.h"

#include <math.h>
#include <stdio.h>

/* The criteria. */
#define ACCENT_INK_LEAST	4.6
#define ACCENT_GROUND_LEAST	3.0
#define ACCENT_TEXT_LEAST	4.6

/* The main button's shade under the pointer (libkeiland's widgets.c WIDGETS_HOVER_SHADE). */
#define ACCENT_SHADE		0.08

/* How many grounds an appearance has: the ground's three colours, a card and a control over the ground. */
#define ACCENT_GROUNDS		5U

static void accent_grounds(unsigned appearance, uint32_t *grounds);
static uint32_t accent_over(uint32_t color, uint32_t under);
static uint32_t accent_mix(uint32_t from, uint32_t to, double amount);
static double accent_contrast(uint32_t a, uint32_t b);
static double accent_luminance(uint32_t color);
static double accent_channel(unsigned value);
static void accent_check(int good, const char *what, unsigned index, unsigned appearance, double value);

/* How many checks were made and how many failed (the test's one thread alone). */
static unsigned accent_checks;
static unsigned accent_failures;

/* Checks every accent in both appearances, and the palettes that take them. */
int
main(void)
{
	struct kl_accent values;
	struct kl_theme theme;
	const struct kl_theme *base;
	uint32_t grounds[ACCENT_GROUNDS];
	uint32_t shaded;
	uint32_t toward;
	unsigned appearance;
	unsigned index;
	unsigned ground;
	double least;
	double value;

	/* Each accent in each appearance. */
	for (appearance = KL_APPEARANCE_LIGHT; appearance <= KL_APPEARANCE_DARK; appearance++) {
		accent_grounds(appearance, grounds);
		for (index = 0; index < KL_ACCENTS; index++) {
			kl_accent_values(index, appearance, &values);

			/* (1) The ink, at rest and shaded away from it. */
			toward = 0xff000000U;
			if (accent_luminance(values.ink) < 0.5)
				toward = 0xffffffffU;
			shaded = accent_mix(values.accent, toward, ACCENT_SHADE);
			least = accent_contrast(values.ink, values.accent);
			value = accent_contrast(values.ink, shaded);
			if (value < least)
				least = value;
			if (index != KL_ACCENT_BLUE)
				accent_check(least >= ACCENT_INK_LEAST, "ink on the accent", index, appearance, least);

			/* (2) The accent on every ground. */
			least = 100.0;
			for (ground = 0; ground < ACCENT_GROUNDS; ground++) {
				value = accent_contrast(values.accent, grounds[ground]);
				if (value < least)
					least = value;
			}
			accent_check(least >= ACCENT_GROUND_LEAST, "accent on the grounds", index, appearance, least);

			/* (3) The accent as text on every ground and on the selection over it. */
			least = 100.0;
			for (ground = 0; ground < ACCENT_GROUNDS; ground++) {
				value = accent_contrast(values.text, grounds[ground]);
				if (value < least)
					least = value;
				value = accent_contrast(values.text, accent_over(values.selection, grounds[ground]));
				if (value < least)
					least = value;
			}
			if (index != KL_ACCENT_BLUE)
				accent_check(least >= ACCENT_TEXT_LEAST, "accent as text", index, appearance, least);

			/* The theme made with it carries its colours. */
			keiui_theme_with(appearance, index, &theme);
			accent_check(theme.accent == values.accent && theme.selection == values.selection &&
			    theme.accent_ink == values.ink && theme.accent_text == values.text, "theme with the accent", index, appearance, 0.0);
		}

		/* Blue is the desktop's colours of before: the base theme's, white ink. */
		base = keiui_theme_of(appearance);
		keiui_theme_with(appearance, KL_ACCENT_BLUE, &theme);
		accent_check(theme.accent == base->accent && theme.selection == base->selection &&
		    theme.accent_ink == KL_RGB(0xffffff) && theme.accent_text == base->accent, "blue as before", 0U, appearance, 0.0);
		accent_check(base->accent == KL_RGB(0x2f7cf6), "blue is 0x2f7cf6", 0U, appearance, 0.0);

		/* An index past the table is blue. */
		kl_accent_values(KL_ACCENTS, appearance, &values);
		accent_check(values.accent == base->accent, "past the table is blue", KL_ACCENTS, appearance, 0.0);

		/* Settings' and Files' palettes take the theme's accent (yellow), and blue gives their own colours back. */
		keiui_theme_set(appearance, KL_ACCENT_YELLOW);
		se_palette_set(appearance);
		fm_palette_set(appearance);
		kl_accent_values(KL_ACCENT_YELLOW, appearance, &values);
		accent_check(se_palette->accent == values.accent && se_palette->accent_text == values.text, "Settings takes the accent", KL_ACCENT_YELLOW, appearance, 0.0);
		accent_check(fm_palette->accent == values.accent && fm_palette->accent_ink == values.ink, "Files takes the accent", KL_ACCENT_YELLOW, appearance, 0.0);
		keiui_theme_set(appearance, KL_ACCENT_BLUE);
		se_palette_set(appearance);
		fm_palette_set(appearance);
		accent_check(se_palette->accent == se_palette_of(appearance)->accent && se_palette->selection == se_palette_of(appearance)->selection, "Settings blue as before", 0U, appearance, 0.0);
		accent_check(fm_palette->accent == fm_palette_of(appearance)->accent && fm_palette->selection == fm_palette_of(appearance)->selection, "Files blue as before", 0U, appearance, 0.0);
	}

	/* The summary. */
	printf("HOST-ACCENT checks=%u failures=%u\n", accent_checks, accent_failures);
	if (accent_failures != 0U) {
		printf("HOST-ACCENT FAIL\n");
		return 1;
	}

	/* Succeeded: every check held. */
	printf("HOST-ACCENT PASS\n");
	return 0;
}

/* Answers the light appearance for libkeiland's theme.c (the program's appearance, here never told). */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	/* No watch here. */
	(void)appearance;
	return KL_APPEARANCE_LIGHT;
}

/* Writes an appearance's grounds: the ground's three colours, a card and a control laid over the ground's top. */
static void
accent_grounds(
	unsigned appearance,
	uint32_t *grounds)
{
	const struct kl_theme *theme;

	/* The theme's colours. */
	theme = keiui_theme_of(appearance);
	grounds[0] = theme->panel;
	grounds[1] = theme->ground_top;
	grounds[2] = theme->ground_bottom;
	grounds[3] = accent_over(theme->card, theme->ground_top);
	grounds[4] = accent_over(theme->control, theme->ground_bottom);
}

/* Lays a translucent colour over an opaque one, channel by channel in sRGB (as the canvas blends). */
static uint32_t
accent_over(
	uint32_t color,
	uint32_t under)
{
	double alpha;
	uint32_t out;
	unsigned shift;
	unsigned top;
	unsigned bottom;

	/* Each channel. */
	alpha = (double)((color >> 24) & 0xffU) / 255.0;
	out = 0xff000000U;
	for (shift = 0; shift <= 16U; shift += 8U) {
		top = (color >> shift) & 0xffU;
		bottom = (under >> shift) & 0xffU;
		out |= (uint32_t)lround((double)top * alpha + (double)bottom * (1.0 - alpha)) << shift;
	}

	/* The blend. */
	return out;
}

/* Mixes two opaque colours (kl_color_mix's way). */
static uint32_t
accent_mix(
	uint32_t from,
	uint32_t to,
	double amount)
{
	uint32_t out;
	unsigned shift;
	unsigned a;
	unsigned b;

	/* Each channel. */
	out = 0xff000000U;
	for (shift = 0; shift <= 16U; shift += 8U) {
		a = (from >> shift) & 0xffU;
		b = (to >> shift) & 0xffU;
		out |= (uint32_t)lround((double)a * (1.0 - amount) + (double)b * amount) << shift;
	}

	/* The mix. */
	return out;
}

/* Reports the WCAG 2 contrast of two opaque colours. */
static double
accent_contrast(
	uint32_t a,
	uint32_t b)
{
	double first;
	double second;

	/* The lighter over the darker. */
	first = accent_luminance(a);
	second = accent_luminance(b);
	if (first < second)
		return (second + 0.05) / (first + 0.05);

	/* The other way round. */
	return (first + 0.05) / (second + 0.05);
}

/* Reports an opaque colour's relative luminance. */
static double
accent_luminance(
	uint32_t color)
{
	double r;
	double g;
	double b;

	/* The channels, linear. */
	r = accent_channel((color >> 16) & 0xffU);
	g = accent_channel((color >> 8) & 0xffU);
	b = accent_channel(color & 0xffU);

	/* WCAG's weights. */
	return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/* Turns an sRGB channel into linear light. */
static double
accent_channel(
	unsigned value)
{
	double channel;

	/* The two pieces of the sRGB curve. */
	channel = (double)value / 255.0;
	if (channel <= 0.03928)
		return channel / 12.92;

	/* The power. */
	return pow((channel + 0.055) / 1.055, 2.4);
}

/* Counts a check, and prints it when it failed. */
static void
accent_check(
	int good,
	const char *what,
	unsigned index,
	unsigned appearance,
	double value)
{
	/* The count, and the line of a failure. */
	accent_checks++;
	if (good)
		return;
	accent_failures++;
	printf("HOST-ACCENT FAIL %s index=%u appearance=%u value=%.2f\n", what, index, appearance, value);
}
