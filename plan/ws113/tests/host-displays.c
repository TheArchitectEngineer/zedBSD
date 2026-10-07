/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the arithmetic and the file of several displays
 * (ws113-p004b, userland/desktop/wayland/displays.c): the mirror's fit, the
 * wallpaper's cover, the checks of the extended places, the place of a
 * display connected later, and displays.conf read and written.
 */

#include "displays.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

static void check(int condition, const char *what);
static void test_fit(void);
static void test_cover(void);
static void test_validate(void);
static void test_place_right(void);
static void test_file(void);

/* Runs every check. */
int
main(void)
{
	/* Each part. */
	test_fit();
	test_cover();
	test_validate();
	test_place_right();
	test_file();

	/* The verdict. */
	if (failures != 0U) {
		printf("host-displays: %u failures\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-displays: PASS\n");
	return 0;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;

	/* One that fails is named. */
	printf("FAIL: %s\n", what);
	failures++;
}

/* The mirror's fit: the desktop whole, centred, bars on the two sides it does not fill. */
static void
test_fit(void)
{
	struct kwl_display_rect fitted;

	/* A wide desktop on a 4:3 display: as wide, bars above and below. */
	kwl_displays_fit(1920U, 1080U, 1024U, 768U, &fitted);
	check(fitted.x == 0 && fitted.y == 96 && fitted.width == 1024U && fitted.height == 576U, "fit 1920x1080 into 1024x768");

	/* A 16:10 desktop on a 16:9 display: as tall, bars at the sides. */
	kwl_displays_fit(1280U, 800U, 1920U, 1080U, &fitted);
	check(fitted.x == 96 && fitted.y == 0 && fitted.width == 1728U && fitted.height == 1080U, "fit 1280x800 into 1920x1080");

	/* The same proportions fill the display. */
	kwl_displays_fit(1280U, 720U, 1920U, 1080U, &fitted);
	check(fitted.x == 0 && fitted.y == 0 && fitted.width == 1920U && fitted.height == 1080U, "fit 1280x720 into 1920x1080");

	/* No size fills it. */
	kwl_displays_fit(0U, 720U, 800U, 600U, &fitted);
	check(fitted.x == 0 && fitted.y == 0 && fitted.width == 800U && fitted.height == 600U, "fit without a size");
}

/* The wallpaper's cover: the middle of the image, its proportions kept. */
static void
test_cover(void)
{
	float uv[4];

	/* A wide image on a 4:3 display loses an eighth at each side. */
	kwl_displays_cover(1920U, 1080U, 1024U, 768U, uv);
	check(fabsf(uv[0] - 0.125f) < 1e-6f && fabsf(uv[2] - 0.875f) < 1e-6f && uv[1] == 0.0f && uv[3] == 1.0f, "cover 1920x1080 on 1024x768");

	/* A 4:3 image on a wide display loses its top and bottom. */
	kwl_displays_cover(1024U, 768U, 1920U, 1080U, uv);
	check(uv[0] == 0.0f && uv[2] == 1.0f && fabsf(uv[1] - 0.125f) < 1e-6f && fabsf(uv[3] - 0.875f) < 1e-6f, "cover 1024x768 on 1920x1080");

	/* The same proportions take the whole image. */
	kwl_displays_cover(1920U, 1080U, 1280U, 720U, uv);
	check(uv[0] == 0.0f && uv[1] == 0.0f && uv[2] == 1.0f && uv[3] == 1.0f, "cover of the same proportions");
}

/* The checks of the extended places. */
static void
test_validate(void)
{
	struct kwl_display_rect rects[3];
	int error;

	/* Side by side, tops aligned. */
	rects[0].x = 0;
	rects[0].y = 0;
	rects[0].width = 1920U;
	rects[0].height = 1080U;
	rects[1].x = 1920;
	rects[1].y = 0;
	rects[1].width = 1024U;
	rects[1].height = 768U;
	error = kwl_displays_validate(rects, 2U);
	check(error == 0, "side by side is joined");

	/* Left of it, lower down but still sharing an edge. */
	rects[1].x = -1024;
	rects[1].y = 500;
	error = kwl_displays_validate(rects, 2U);
	check(error == 0, "left and lower, sharing an edge");

	/* Below it, sharing a part of the bottom edge. */
	rects[1].x = 1500;
	rects[1].y = 1080;
	error = kwl_displays_validate(rects, 2U);
	check(error == 0, "below, sharing a part of the bottom edge");

	/* Touching at a corner only. */
	rects[1].x = 1920;
	rects[1].y = 1080;
	error = kwl_displays_validate(rects, 2U);
	check(error == ENOTCONN, "a corner does not join");

	/* A gap. */
	rects[1].x = 1921;
	rects[1].y = 0;
	error = kwl_displays_validate(rects, 2U);
	check(error == ENOTCONN, "a gap does not join");

	/* An overlap of one column. */
	rects[1].x = 1919;
	error = kwl_displays_validate(rects, 2U);
	check(error == EINVAL, "an overlap is refused");

	/* Three in a row, the third joined through the second. */
	rects[1].x = 1920;
	rects[2].x = 2944;
	rects[2].y = 0;
	rects[2].width = 800U;
	rects[2].height = 600U;
	error = kwl_displays_validate(rects, 3U);
	check(error == 0, "three in a row are joined");

	/* A display without a size, and one far away. */
	rects[2].width = 0U;
	error = kwl_displays_validate(rects, 3U);
	check(error == ERANGE, "a display without a size");
	rects[2].width = 800U;
	rects[2].x = KWL_DISPLAYS_LIMIT + 1;
	error = kwl_displays_validate(rects, 3U);
	check(error == ERANGE, "a display out of range");

	/* One, and none. */
	error = kwl_displays_validate(rects, 1U);
	check(error == 0, "one display");
	error = kwl_displays_validate(rects, 0U);
	check(error == 0, "no display");
}

/* The place of a display connected later. */
static void
test_place_right(void)
{
	struct kwl_display_rect rects[2];
	int32_t x;
	int32_t y;

	/* No display: the origin. */
	kwl_displays_place_right(rects, 0U, &x, &y);
	check(x == 0 && y == 0, "place without a display");

	/* Right of the rightmost, at its top. */
	rects[0].x = 0;
	rects[0].y = 0;
	rects[0].width = 1920U;
	rects[0].height = 1080U;
	rects[1].x = -1280;
	rects[1].y = 200;
	rects[1].width = 1280U;
	rects[1].height = 800U;
	kwl_displays_place_right(rects, 2U, &x, &y);
	check(x == 1920 && y == 0, "place right of the rightmost");
	rects[1].x = 1920;
	kwl_displays_place_right(rects, 2U, &x, &y);
	check(x == 3200 && y == 200, "place right of the second");
}

/* displays.conf read and written. */
static void
test_file(void)
{
	static const char text[] =
		"version=1\r\n"
		"mode=mirror\n"
		"anchor=zedbsd-port-v1:pci:0000:00:02.0:edp:A\n"
		"brightness=40\n"
		"place=zedbsd-port-v1:pci:0000:00:02.0:edp:A 0 0\n"
		"place=zedbsd-port-v1:pci:0000:00:02.0:hdmi:B -1280 120\n"
		"place=broken\n"
		"place=Venus virtual display 1 -1024 0\n"
		"place=1 2\n"
		"no equals here\n";
	struct kwl_display_config config;
	struct kwl_display_config again;
	char written[1024];
	char key[80];
	int32_t x;
	int32_t y;
	size_t length;
	unsigned index;
	int error;
	int found;

	/* The text read: the mode, the anchor, two places; the unknown key and the broken places skipped. */
	error = kwl_displays_parse(text, sizeof(text) - 1U, &config);
	check(error == 0, "the file is read");
	check(config.mode == KWL_DISPLAYS_MIRROR, "the mode is mirror");
	check(strcmp(config.anchor, "zedbsd-port-v1:pci:0000:00:02.0:edp:A") == 0, "the anchor");
	check(config.has_brightness == 1U && config.brightness == 40U, "the panel's light");
	check(config.count == 3U, "three places");
	found = kwl_displays_find(&config, "Venus virtual display 1");
	check(found == 2 && config.places[2].x == -1024 && config.places[2].y == 0, "a key with spaces");
	found = kwl_displays_find(&config, "zedbsd-port-v1:pci:0000:00:02.0:hdmi:B");
	check(found == 1 && config.places[1].x == -1280 && config.places[1].y == 120, "the HDMI display's place");

	/* Written and read again, the same choice. */
	length = kwl_displays_format(&config, written, sizeof(written));
	check(length > 0U && length == strlen(written), "the file is written");
	error = kwl_displays_parse(written, length, &again);
	check(error == 0 && again.mode == config.mode && again.count == config.count, "written and read again");
	check(strcmp(again.places[0].key, config.places[0].key) == 0 && again.places[1].x == -1280, "the places again");
	check(again.has_brightness == 1U && again.brightness == 40U, "the light again");
	error = kwl_displays_parse("version=1\nbrightness=101\n", 26U, &again);
	check(error == 0 && again.has_brightness == 0U, "a light out of range is skipped");

	/* Too small a buffer writes nothing. */
	length = kwl_displays_format(&config, written, 20U);
	check(length == 0U, "a buffer too small");

	/* Another version is not read; nor a text without one. */
	error = kwl_displays_parse("version=2\nmode=mirror\n", 22U, &again);
	check(error == ENOTSUP && again.mode == KWL_DISPLAYS_EXTENDED && again.count == 0U, "another version");
	error = kwl_displays_parse("mode=mirror\n", 12U, &again);
	check(error == EINVAL && again.mode == KWL_DISPLAYS_EXTENDED, "no version");

	/* A place changed, then as many as the choice keeps, then one more. */
	error = kwl_displays_set(&config, "zedbsd-port-v1:pci:0000:00:02.0:hdmi:B", 1920, 0);
	check(error == 0 && config.count == 3U && config.places[1].x == 1920, "a place changed");
	for (index = config.count; index < KWL_DISPLAYS_PLACES; index++) {
		(void)snprintf(key, sizeof(key), "display-%u", index);
		error = kwl_displays_set(&config, key, (int32_t)index, 0);
		check(error == 0, "a place added");
	}

	/* No room for one more. */
	error = kwl_displays_set(&config, "one-too-many", 0, 0);
	check(error == ENOSPC, "no room for another place");

	/* A place's text: the key may hold spaces, the coordinates are the last two words. */
	error = kwl_displays_parse_place("Venus virtual display 0 12 -34", key, sizeof(key), &x, &y);
	check(error == 0 && strcmp(key, "Venus virtual display 0") == 0 && x == 12 && y == -34, "a place's text with spaces");
	error = kwl_displays_parse_place("12 34", key, sizeof(key), &x, &y);
	check(error == EINVAL, "a place's text without a key");
	error = kwl_displays_parse_place("key 12 3x", key, sizeof(key), &x, &y);
	check(error == EINVAL, "a place's text with a bad coordinate");

	/* A key too long is refused. */
	memset(key, 'k', sizeof(key));
	key[sizeof(key) - 1U] = '\0';
	error = kwl_displays_set(&config, key, 0, 0);
	check(error == EINVAL, "a key too long");
}
