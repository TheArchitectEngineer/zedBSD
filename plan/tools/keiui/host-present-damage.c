/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws210-p013 E6: the host test of the swapchain images' stale parts
 * (userland/desktop/libkeiland/ui/present-damage.c), with a model of the
 * pixels.  A staging frame changes by random parts (or whole) as
 * present.c writes it; every image adds the parts; a random image is
 * given each frame (the same one again, too), with the acquire, the
 * submit and the present failing at random, and new sizes; the image
 * given takes only its regions from the staging frame (an image whose
 * contents are not known first holds garbage).  After each frame
 * submitted the image is the staging frame, pixel for pixel; the regions
 * are at most KEIUI_STALE_RECTS, inside the frame, apart, and their edges
 * on KEIUI_STALE_ALIGN columns (or the frame's right edge).  And the
 * copy into an image of R, G, B, A swaps red and blue alone, inside its area.
 *
 *     host-present-damage      prints "host-present-damage: N checks, F failures"
 */

#include "userland/desktop/libkeiland/ui/present-copy.h"
#include "userland/desktop/libkeiland/ui/present-damage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The largest frame, the most images, and how many frames are tried. */
#define TEST_MAX_WIDTH	200
#define TEST_MAX_HEIGHT	120
#define TEST_IMAGES	4U
#define TEST_FRAMES	200000U

/* The checks made and those that failed. */
static unsigned long checks;
static unsigned long failures;

/* The staging frame and the images' pixels (a frame's number each), and the frame's size. */
static uint32_t staging[TEST_MAX_WIDTH * TEST_MAX_HEIGHT];
static uint32_t images[TEST_IMAGES][TEST_MAX_WIDTH * TEST_MAX_HEIGHT];
static uint32_t width;
static uint32_t height;

int main(void);
static void check(int condition, const char *what, unsigned long frame);
static int random_below(int limit);
static void random_part(struct kl_rect *part);
static void write_part(const struct kl_rect *part, uint32_t value);
static void check_regions(const struct kl_rect *regions, unsigned count, unsigned long frame);
static void check_swap(void);

/*
 * Runs the frames and reports the checks.
 */
int
main(void)
{
	static struct keiui_stale stale[TEST_IMAGES];
	struct kl_rect regions[KEIUI_STALE_RECTS];
	struct kl_rect parts[4];
	struct kl_rect whole;
	unsigned long frame;
	unsigned count;
	unsigned images_used;
	unsigned part_count;
	unsigned index;
	unsigned image;
	int fail;
	int x;
	int y;
	int same;

	/* The first size: every image not known, the staging frame written whole. */
	srand(213);
	width = 160U;
	height = 100U;
	images_used = 3U;
	for (image = 0U; image < TEST_IMAGES; image++)
		keiui_stale_unknown(&stale[image]);
	whole.x = 0;
	whole.y = 0;
	whole.width = (int)width;
	whole.height = (int)height;
	write_part(&whole, 1U);

	/* Each frame. */
	for (frame = 2U; frame < TEST_FRAMES; frame++) {
		/* Now and then a new size and number of images: all not known, the staging frame new and written whole. */
		if (random_below(500) == 0) {
			width = (uint32_t)(1 + random_below(TEST_MAX_WIDTH));
			height = (uint32_t)(1 + random_below(TEST_MAX_HEIGHT));
			images_used = (unsigned)(2 + random_below((int)TEST_IMAGES - 1));
			for (image = 0U; image < TEST_IMAGES; image++)
				keiui_stale_unknown(&stale[image]);
			whole.width = (int)width;
			whole.height = (int)height;
			write_part(&whole, (uint32_t)frame);
			continue;
		}

		/* The parts of the frame: one to four, or the whole frame now and then. */
		part_count = (unsigned)(1 + random_below(4));
		for (index = 0U; index < part_count; index++)
			random_part(&parts[index]);
		if (random_below(40) == 0) {
			whole.width = (int)width;
			whole.height = (int)height;
			parts[0] = whole;
			part_count = 1U;
		}

		/* Written into the staging frame, and added to every image's stale parts. */
		for (index = 0U; index < part_count; index++) {
			write_part(&parts[index], (uint32_t)frame);
			for (image = 0U; image < images_used; image++)
				keiui_stale_add(&stale[image], width, height, &parts[index]);
		}

		/* The image given (a failed acquire gives none). */
		fail = random_below(50);
		if (fail == 0)
			continue;
		image = (unsigned)random_below((int)images_used);

		/* The regions it takes, as they must be. */
		count = keiui_stale_regions(&stale[image], width, height, regions);
		check_regions(regions, count, frame);

		/* A failed submit: nothing written, every image not known. */
		if (fail == 1) {
			for (index = 0U; index < images_used; index++)
				keiui_stale_unknown(&stale[index]);
			continue;
		}

		/* An image not known holds garbage before the copy (its layout was undefined). */
		if (stale[image].whole) {
			for (y = 0; y < (int)height; y++) {
				for (x = 0; x < (int)width; x++)
					images[image][y * TEST_MAX_WIDTH + x] = 0xdeadbeefU;
			}
		}

		/* The regions copied, the image then holding the frame. */
		for (index = 0U; index < count; index++) {
			for (y = regions[index].y; y < regions[index].y + regions[index].height; y++) {
				for (x = regions[index].x; x < regions[index].x + regions[index].width; x++)
					images[image][y * TEST_MAX_WIDTH + x] = staging[y * TEST_MAX_WIDTH + x];
			}
		}
		keiui_stale_clear(&stale[image]);

		/* It is the staging frame. */
		same = 1;
		for (y = 0; y < (int)height && same; y++) {
			for (x = 0; x < (int)width; x++) {
				if (images[image][y * TEST_MAX_WIDTH + x] != staging[y * TEST_MAX_WIDTH + x]) {
					same = 0;
					break;
				}
			}
		}
		check(same, "image is the staging frame", frame);

		/* A failed present after the copy: every image not known. */
		if (fail == 2) {
			for (index = 0U; index < images_used; index++)
				keiui_stale_unknown(&stale[index]);
		}
	}

	/* The copy that swaps red and blue. */
	check_swap();

	/* The result. */
	printf("host-present-damage: %lu checks, %lu failures\n", checks, failures);
	if (failures != 0U)
		return 1;
	return 0;
}

/* Counts a check, and prints one that failed (the first few). */
static void
check(
	int condition,
	const char *what,
	unsigned long frame)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	if (failures <= 10U)
		printf("FAIL frame=%lu %s (%ux%u)\n", frame, what, width, height);
}

/* Reports a random number from 0 below a limit. */
static int
random_below(
	int limit)
{
	/* The C library's numbers, folded. */
	return rand() % limit;
}

/* Makes a random part, partly outside the frame at times, small mostly. */
static void
random_part(
	struct kl_rect *part)
{
	/* Its place, a little outside now and then. */
	part->x = random_below((int)width + 20) - 10;
	part->y = random_below((int)height + 20) - 10;

	/* Its size: small mostly, large at times. */
	part->width = 1 + random_below(24);
	part->height = 1 + random_below(24);
	if (random_below(10) == 0) {
		part->width = 1 + random_below((int)width);
		part->height = 1 + random_below((int)height);
	}
}

/* Writes a value over the part of the staging frame inside the frame. */
static void
write_part(
	const struct kl_rect *part,
	uint32_t value)
{
	int x;
	int y;

	/* Each pixel of the part inside the frame. */
	for (y = part->y; y < part->y + part->height; y++) {
		for (x = part->x; x < part->x + part->width; x++) {
			if (x >= 0 && y >= 0 && x < (int)width && y < (int)height)
				staging[y * TEST_MAX_WIDTH + x] = value;
		}
	}
}

/* Checks a list of regions: few, inside the frame, apart, and aligned. */
static void
check_regions(
	const struct kl_rect *regions,
	unsigned count,
	unsigned long frame)
{
	unsigned first;
	unsigned second;
	int apart;
	int right;

	/* Few. */
	check(count <= KEIUI_STALE_RECTS, "at most KEIUI_STALE_RECTS regions", frame);

	/* Each inside the frame, not empty, with aligned edges. */
	for (first = 0U; first < count; first++) {
		right = regions[first].x + regions[first].width;
		check(regions[first].x >= 0 && regions[first].y >= 0, "region inside the frame (left, top)", frame);
		check(right <= (int)width && regions[first].y + regions[first].height <= (int)height, "region inside the frame (right, bottom)", frame);
		check(regions[first].width > 0 && regions[first].height > 0, "region not empty", frame);
		check(regions[first].x % KEIUI_STALE_ALIGN == 0, "left edge aligned", frame);
		check(right % KEIUI_STALE_ALIGN == 0 || right == (int)width, "right edge aligned", frame);
	}

	/* Apart from each other. */
	for (first = 0U; first < count; first++) {
		for (second = first + 1U; second < count; second++) {
			apart = 0;
			if (regions[first].x + regions[first].width <= regions[second].x ||
			    regions[second].x + regions[second].width <= regions[first].x ||
			    regions[first].y + regions[first].height <= regions[second].y ||
			    regions[second].y + regions[second].height <= regions[first].y)
				apart = 1;
			check(apart, "regions apart", frame);
		}
	}
}

/* Checks the copy with red and blue swapped: inside its area each pixel's first and third bytes change places, outside nothing changes. */
static void
check_swap(void)
{
	static const uint32_t pixels[4 * 3] = {
		0x11223344U, 0x55667788U, 0x99aabbccU, 0xddeeff00U,
		0x01020304U, 0x05060708U, 0x090a0b0cU, 0x0d0e0f10U,
		0xa1a2a3a4U, 0xb1b2b3b4U, 0xc1c2c3c4U, 0xd1d2d3d4U
	};
	uint32_t canvas[8 * 3];
	struct kl_rect area;

	/* Columns 1 and 2 of rows 1 and 2, into a canvas of 32-byte rows. */
	memset(canvas, 0, sizeof(canvas));
	area.x = 1;
	area.y = 1;
	area.width = 2;
	area.height = 2;
	keiui_present_copy_swap((unsigned char *)canvas, 8U * 4U, pixels, 4U, &area);
	check(canvas[8 + 1] == 0x05080706U && canvas[8 + 2] == 0x090c0b0aU, "swap of row 1", 0UL);
	check(canvas[16 + 1] == 0xb1b4b3b2U && canvas[16 + 2] == 0xc1c4c3c2U, "swap of row 2", 0UL);
	check(canvas[0 + 1] == 0U && canvas[8 + 0] == 0U && canvas[8 + 3] == 0U, "nothing outside the area", 0UL);
}
