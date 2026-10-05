/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p017: the contrast of the text in the light and the dark
 * appearance's colours (WCAG 2, 4.5 or more, the criterion C7 of WS099).
 * Each pair is a text colour on its ground as the program draws it: a
 * translucent ground laid over what is under it (sRGB, as the canvas
 * blends), down to the compositor's glass at its worst for the appearance
 * -- the white glass at its least brightness (GLASS_LEAST_LUMA 0.85) under
 * dark text, the dark glass at its most (GLASS_MOST_LUMA 0.15) under light
 * text (userland/desktop/wayland/shaders/panel.frag).
 *
 * The colours are the programs' own: libkeiland's themes (ui/theme.c),
 * Settings' palette (settings/palette.c) and Files' (files/palette.c),
 * compiled in.  The faint text (inactive items, which WCAG exempts) is not
 * counted.  Last line: HOST-DARK PASS.
 */

#include <keiland.h>

#include "userland/desktop/libkeiland/ui/internal.h"
#include "userland/desktop/settings/settings.h"
#include "userland/desktop/files/files.h"

#include <math.h>
#include <stdio.h>

/* The least contrast of text, and the glass's brightness at its worst for each appearance. */
#define DARK_LEAST_CONTRAST	4.5
#define DARK_GLASS_LIGHT	0.85
#define DARK_GLASS_DARK		0.15

/* A colour as three channels of 0 to 1. */
struct dark_rgb {
	double r;
	double g;
	double b;
};

static struct dark_rgb dark_glass(unsigned appearance);
static struct dark_rgb dark_over(uint32_t color, struct dark_rgb under);
static double dark_luminance(struct dark_rgb color);
static double dark_channel(double value);
static void dark_check(const char *name, unsigned appearance, uint32_t text, struct dark_rgb ground);

/* How many pairs were measured and how many fell short. */
static unsigned dark_pairs;
static unsigned dark_failures;

/* Measures every pair in both appearances. */
int
main(void)
{
	const struct kl_theme *theme;
	const struct se_palette *se;
	const struct fm_palette *fm;
	struct dark_rgb glass;
	struct dark_rgb ground;
	struct dark_rgb card;
	unsigned appearance;

	/* The light appearance, then the dark one. */
	for (appearance = KL_APPEARANCE_LIGHT; appearance <= KL_APPEARANCE_DARK; appearance++) {
		glass = dark_glass(appearance);

		/* libkeiland's theme: on its panel, on a card on its ground, on a control on the card, and on its glass veils. */
		theme = keiui_theme_of(appearance);
		ground = dark_over(theme->panel, glass);
		dark_check("kl panel text", appearance, theme->text, ground);
		dark_check("kl panel text_secondary", appearance, theme->text_secondary, ground);
		dark_check("kl panel icon", appearance, theme->icon, ground);
		card = dark_over(theme->card, dark_over(theme->ground_top, glass));
		dark_check("kl card text", appearance, theme->text, card);
		dark_check("kl card text_secondary", appearance, theme->text_secondary, card);
		dark_check("kl control text", appearance, theme->text, dark_over(theme->control, card));
		ground = dark_over(theme->glass_content, glass);
		dark_check("kl glass_content text", appearance, theme->text, ground);
		dark_check("kl glass_content text_secondary", appearance, theme->text_secondary, ground);
		ground = dark_over(theme->glass_sidebar, glass);
		dark_check("kl glass_sidebar text", appearance, theme->text, ground);
		dark_check("kl glass_sidebar text_secondary", appearance, theme->text_secondary, ground);
		dark_check("kl selection text", appearance, theme->text, dark_over(theme->selection, ground));

		/* Settings: the list of pages, the page, a card on it, a control, a field and a tile. */
		se = se_palette_of(appearance);
		ground = dark_over(se->glass_sidebar, glass);
		dark_check("se sidebar text", appearance, se->text, ground);
		dark_check("se sidebar text_secondary", appearance, se->text_secondary, ground);
		dark_check("se sidebar selected", appearance, se->text, dark_over(se->selection, ground));
		ground = dark_over(se->glass_page, glass);
		dark_check("se page text", appearance, se->text, ground);
		dark_check("se page text_secondary", appearance, se->text_secondary, ground);
		card = dark_over(se->card, ground);
		dark_check("se card text", appearance, se->text, card);
		dark_check("se card text_secondary", appearance, se->text_secondary, card);
		dark_check("se card title", appearance, se->title, card);
		dark_check("se control text", appearance, se->text, dark_over(se->control, card));
		dark_check("se field text", appearance, se->text, dark_over(se->field, card));
		dark_check("se tile text", appearance, se->text, dark_over(se->tile, ground));
		dark_check("se tile hover text", appearance, se->text, dark_over(se->tile_hover, ground));
		dark_check("se tile text_secondary", appearance, se->text_secondary, dark_over(se->tile, ground));

		/* Files: the panel, the glass veils, a button, a card's inner ground and an icon's tile. */
		fm = fm_palette_of(appearance);
		dark_check("fm panel text", appearance, fm->text, dark_over(fm->panel, glass));
		dark_check("fm panel text_secondary", appearance, fm->text_secondary, dark_over(fm->panel, glass));
		ground = dark_over(fm->glass_sidebar, glass);
		dark_check("fm sidebar text", appearance, fm->text, ground);
		dark_check("fm sidebar text_secondary", appearance, fm->text_secondary, ground);
		dark_check("fm sidebar selected", appearance, fm->text, dark_over(fm->selection, ground));
		ground = dark_over(fm->glass_content, glass);
		dark_check("fm content text", appearance, fm->text, ground);
		dark_check("fm content text_secondary", appearance, fm->text_secondary, ground);
		dark_check("fm content selected", appearance, fm->text, dark_over(fm->selection, ground));
		dark_check("fm button text", appearance, fm->text, dark_over(fm->button, ground));
		dark_check("fm button lit text", appearance, fm->text, dark_over(fm->button_lit, ground));
		dark_check("fm inner text", appearance, fm->text, dark_over(fm->inner, ground));
		dark_check("fm title", appearance, fm->title, dark_over(fm->panel, glass));
	}

	/* The result. */
	printf("HOST-DARK pairs=%u failures=%u\n", dark_pairs, dark_failures);
	if (dark_failures != 0U) {
		printf("HOST-DARK FAIL\n");
		return 1;
	}

	/* Every pair is enough. */
	printf("HOST-DARK PASS\n");
	return 0;
}

/* The compositor's glass at its worst for the appearance's text: grey of the brightness limit. */
static struct dark_rgb
dark_glass(
	unsigned appearance)
{
	struct dark_rgb glass;

	/* The white glass's least brightness, or the dark glass's most. */
	glass.r = DARK_GLASS_LIGHT;
	if (appearance == KL_APPEARANCE_DARK)
		glass.r = DARK_GLASS_DARK;
	glass.g = glass.r;
	glass.b = glass.r;
	return glass;
}

/* Lays a colour (its opacity in the top byte) over another. */
static struct dark_rgb
dark_over(
	uint32_t color,
	struct dark_rgb under)
{
	struct dark_rgb result;
	double alpha;

	/* Each channel mixed by the opacity. */
	alpha = (double)((color >> 24) & 0xffU) / 255.0;
	result.r = alpha * (double)((color >> 16) & 0xffU) / 255.0 + (1.0 - alpha) * under.r;
	result.g = alpha * (double)((color >> 8) & 0xffU) / 255.0 + (1.0 - alpha) * under.g;
	result.b = alpha * (double)(color & 0xffU) / 255.0 + (1.0 - alpha) * under.b;
	return result;
}

/* The relative luminance (WCAG 2). */
static double
dark_luminance(
	struct dark_rgb color)
{
	double red;
	double green;
	double blue;

	/* The linear channels, weighted. */
	red = dark_channel(color.r);
	green = dark_channel(color.g);
	blue = dark_channel(color.b);
	return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
}

/* One sRGB channel made linear. */
static double
dark_channel(
	double value)
{
	/* The straight part near black. */
	if (value <= 0.04045)
		return value / 12.92;

	/* The curve. */
	return pow((value + 0.055) / 1.055, 2.4);
}

/* Measures one text colour on its ground and counts it. */
static void
dark_check(
	const char *name,
	unsigned appearance,
	uint32_t text,
	struct dark_rgb ground)
{
	struct dark_rgb ink;
	double light;
	double dark;
	double contrast;
	double swap;
	const char *look;
	const char *verdict;

	/* The text's luminance and the ground's, the lighter first. */
	ink = dark_over(text | 0xff000000U, ground);
	light = dark_luminance(ink);
	dark = dark_luminance(ground);
	if (dark > light) {
		swap = light;
		light = dark;
		dark = swap;
	}

	/* The ratio, and whether it is enough. */
	contrast = (light + 0.05) / (dark + 0.05);
	dark_pairs++;
	verdict = "ok";
	if (contrast < DARK_LEAST_CONTRAST) {
		dark_failures++;
		verdict = "FAIL";
	}

	/* The line. */
	look = "light";
	if (appearance == KL_APPEARANCE_DARK)
		look = "dark";
	printf("HOST-DARK %s %s contrast=%.2f %s\n", look, name, contrast, verdict);
}
