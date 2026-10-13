/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the displays' rotations in displays.conf (ws217-p002a,
 * userland/desktop/wayland/displays.c): "rotate=KEY N" read with a key that
 * holds spaces (N is the last word), lines that are not one dropped and
 * counted, the angle changed and taken out, as many as the file keeps, the
 * line written after the others, the text read back the same, a file
 * without a rotation written as before, and the fullest file within the
 * text the compositor reads.
 */

#include "displays.h"
#include "rotate.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The number of failed checks. */
static unsigned failures;

static void check(int condition, const char *what);
static void test_read(void);
static void test_dropped(void);
static void test_set(void);
static void test_write(void);

/* Runs every check. */
int
main(void)
{
	/* Each part. */
	test_read();
	test_dropped();
	test_set();
	test_write();

	/* The verdict. */
	if (failures != 0U) {
		printf("host-displays-conf: %u failures\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-displays-conf: PASS\n");
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

/* A file with rotations among the other lines: each read by its key, the others as before. */
static void
test_read(void)
{
	static const char text[] =
		"version=1\n"
		"mode=extended\n"
		"place=Venus virtual display 0 0 0\n"
		"rotate=Venus virtual display 0 90\n"
		"rotate=zedbsd-port-v1:pci:0000:00:02.0:edp:A 270\r\n"
		"off=zedbsd-port-v1:pci:0000:00:02.0:hdmi:B\n"
		"rotate=zedbsd-port-v1:pci:0000:00:02.0:hdmi:B 180\n";
	struct kwl_display_config config;
	unsigned rotation;
	int error;

	/* The whole text. */
	error = kwl_displays_parse(text, sizeof(text) - 1U, &config);
	check(error == 0, "the file with rotations is read");
	check(config.count == 1U && config.off_count == 1U, "the places and the displays off are read as before");
	check(config.rotation_count == 3U && config.rotation_dropped == 0U, "three rotations, none dropped");

	/* Each display's angle by its key; a key with spaces keeps them. */
	rotation = kwl_displays_rotation(&config, "Venus virtual display 0");
	check(rotation == KWL_ROTATE_90, "a key with spaces: 90");
	rotation = kwl_displays_rotation(&config, "zedbsd-port-v1:pci:0000:00:02.0:edp:A");
	check(rotation == KWL_ROTATE_270, "a line ended by a carriage return: 270");
	rotation = kwl_displays_rotation(&config, "zedbsd-port-v1:pci:0000:00:02.0:hdmi:B");
	check(rotation == KWL_ROTATE_180, "a display off keeps its rotation: 180");
	rotation = kwl_displays_rotation(&config, "Venus virtual display 1");
	check(rotation == KWL_ROTATE_0, "a display without a line is unrotated");
	rotation = kwl_displays_rotation(&config, "Venus virtual display");
	check(rotation == KWL_ROTATE_0, "a key is matched whole");

	/* The same key twice: the last line counts. */
	error = kwl_displays_parse("version=1\nrotate=A 90\nrotate=A 270\n", 36U, &config);
	check(error == 0 && config.rotation_count == 1U, "the same key twice keeps one line");
	rotation = kwl_displays_rotation(&config, "A");
	check(rotation == KWL_ROTATE_270, "the last line of a key counts");
}

/* Lines that are not a rotation are dropped and counted; the rest of the file is read. */
static void
test_dropped(void)
{
	static const char *const bad[] = {
		"rotate=A 45", "rotate=A 0", "rotate=A 360", "rotate=A -90", "rotate=A +90", "rotate=A 90x", "rotate=A ",
		"rotate=90", "rotate= 90", "rotate=A 9999999999999999999999"
	};
	struct kwl_display_config config;
	char text[256];
	unsigned rotation;
	unsigned index;
	int length;
	int error;

	/* Each bad line beside a good one. */
	for (index = 0U; index < sizeof(bad) / sizeof(bad[0]); index++) {
		length = snprintf(text, sizeof(text), "version=1\n%s\nrotate=B 180\nmode=mirror\n", bad[index]);
		error = kwl_displays_parse(text, (size_t)length, &config);
		check(error == 0, "a file with a bad rotation is read");
		check(config.rotation_dropped == 1U, bad[index]);
		check(config.mode == KWL_DISPLAYS_MIRROR, "the lines after a bad rotation are read");
		rotation = kwl_displays_rotation(&config, "B");
		check(rotation == KWL_ROTATE_180 && config.rotation_count == 1U, "the good rotation beside a bad one is kept");
	}

	/* A key too long for the file. */
	length = snprintf(text, sizeof(text), "version=1\nrotate=%s 90\n",
	    "0123456789012345678901234567890123456789012345678901234567890123456789");
	error = kwl_displays_parse(text, (size_t)length, &config);
	check(error == 0 && config.rotation_count == 0U && config.rotation_dropped == 1U, "a key too long is dropped");
}

/* The angle changed, taken out for 0, refused for a number that is no angle, and the limit. */
static void
test_set(void)
{
	struct kwl_display_config config;
	char key[KWL_DISPLAYS_KEY];
	unsigned rotation;
	unsigned index;
	int error;

	/* A new line, its angle changed, then taken out. */
	kwl_displays_config_init(&config);
	error = kwl_displays_set_rotation(&config, "Venus virtual display 0", KWL_ROTATE_90);
	check(error == 0 && config.rotation_count == 1U, "a rotation is kept");
	error = kwl_displays_set_rotation(&config, "Venus virtual display 0", KWL_ROTATE_270);
	rotation = kwl_displays_rotation(&config, "Venus virtual display 0");
	check(error == 0 && config.rotation_count == 1U && rotation == KWL_ROTATE_270, "its angle changes in place");
	error = kwl_displays_set_rotation(&config, "Venus virtual display 0", KWL_ROTATE_0);
	check(error == 0 && config.rotation_count == 0U, "0 takes the line out");
	error = kwl_displays_set_rotation(&config, "Venus virtual display 0", KWL_ROTATE_0);
	check(error == 0 && config.rotation_count == 0U, "0 for a display unrotated already changes nothing");

	/* What is not kept. */
	error = kwl_displays_set_rotation(&config, "A", 45U);
	check(error == EINVAL, "45 is refused");
	error = kwl_displays_set_rotation(&config, "", KWL_ROTATE_90);
	check(error == EINVAL, "an empty key is refused");

	/* As many as the file keeps, the middle one taken out with the others kept in order. */
	for (index = 0U; index < KWL_DISPLAYS_PLACES; index++) {
		(void)snprintf(key, sizeof(key), "D%u", index);
		error = kwl_displays_set_rotation(&config, key, KWL_ROTATE_180);
		check(error == 0, "a rotation within the limit is kept");
	}

	/* One more is refused; one taken out of the middle leaves the rest in order. */
	error = kwl_displays_set_rotation(&config, "one too many", KWL_ROTATE_90);
	check(error == ENOSPC, "one too many is refused");
	error = kwl_displays_set_rotation(&config, "D5", KWL_ROTATE_0);
	check(error == 0 && config.rotation_count == KWL_DISPLAYS_PLACES - 1U, "the middle one is taken out");
	check(strcmp(config.rotations[5].key, "D6") == 0 && strcmp(config.rotations[14].key, "D15") == 0, "the others keep their order");
}

/* The text written: the rotations last, read back the same; a file without one is written as before. */
static void
test_write(void)
{
	struct kwl_display_config config;
	struct kwl_display_config again;
	char text[8192];
	const char *rotate;
	const char *off;
	size_t length;
	unsigned rotation;
	unsigned index;
	int error;

	/* Without a rotation: no rotate line. */
	kwl_displays_config_init(&config);
	(void)kwl_displays_set(&config, "A", 0, 0);
	(void)kwl_displays_set_off(&config, "B", 1U);
	length = kwl_displays_format(&config, text, sizeof(text));
	check(length != 0U && strstr(text, "rotate=") == NULL, "no rotation, no line");
	check(strcmp(text, "version=1\nmode=extended\nplace=A 0 0\noff=B\n") == 0, "a file without a rotation is written as before");

	/* With rotations: after the displays off, read back the same. */
	(void)kwl_displays_set_rotation(&config, "Venus virtual display 0", KWL_ROTATE_90);
	(void)kwl_displays_set_rotation(&config, "B", KWL_ROTATE_180);
	length = kwl_displays_format(&config, text, sizeof(text));
	check(length != 0U, "the file with rotations is written");
	rotate = strstr(text, "rotate=Venus virtual display 0 90\n");
	off = strstr(text, "off=B\n");
	check(rotate != NULL && off != NULL && rotate > off, "the rotation lines come after the displays off");
	error = kwl_displays_parse(text, length, &again);
	rotation = kwl_displays_rotation(&again, "B");
	check(error == 0 && again.rotation_count == 2U && rotation == KWL_ROTATE_180, "the text is read back the same");

	/* A full file of the longest keys, the rotations with it, still fits the compositor's text (heads.c, 4096 bytes). */
	kwl_displays_config_init(&config);
	for (index = 0U; index < KWL_DISPLAYS_PLACES; index++) {
		(void)snprintf(text, KWL_DISPLAYS_KEY, "%0*u", (int)(KWL_DISPLAYS_KEY - 1U), index);
		(void)kwl_displays_set(&config, text, -(int32_t)KWL_DISPLAYS_LIMIT, -(int32_t)KWL_DISPLAYS_LIMIT);
		(void)kwl_displays_set_off(&config, text, 1U);
		(void)kwl_displays_set_rotation(&config, text, KWL_ROTATE_270);
	}

	/* The anchor and the light too, then the whole text. */
	(void)snprintf(config.anchor, sizeof(config.anchor), "%s", config.places[0].key);
	config.has_brightness = 1U;
	config.brightness = 100U;
	length = kwl_displays_format(&config, text, sizeof(text));
	check(length != 0U && length < 4096U, "the fullest file fits 4096 bytes");
}
