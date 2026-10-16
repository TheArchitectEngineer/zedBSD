/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's display words (stages A6 to A8): the
 * cursor's place against the plane (inside, hanging over the left and top
 * edges, wholly outside each edge), the cursor's control, size and timing
 * words and the pixel processor's, the 24-bit frame count's advance, the
 * refresh from counted frames, and the interrupt words of a frame's start,
 * a finished flip and a hot-plug line (an acknowledgement is written only
 * when asked, the other bits keep what was read).  The expected words are
 * written from the Linux field masks the driver's comments name.
 *
 * Each case prints "PASS name" or "FAIL name: why"; the exit status is 1
 * when any case failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void test_cursor_place(void);
static void test_cursor_words(void);
static void test_frames(void);
static void test_interrupt_words(void);

int
main(void)
{
	/* Runs the cases. */
	test_cursor_place();
	test_cursor_words();
	test_frames();
	test_interrupt_words();

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-display-test: %d case(s)\n", failures);
		return 1;
	}

	/* Every case passed. */
	printf("PASS amdrdna-display-test\n");
	return 0;
}

/* Prints one case's result and counts a failure. */
static void
check(
	int condition,
	const char *name,
	const char *why)
{
	/* A failed case says why. */
	if (!condition) {
		printf("FAIL %s: %s\n", name, why);
		failures++;
		return;
	}

	/* A passed case. */
	printf("PASS %s\n", name);
}

/* The cursor's place: position and hot-spot words, and whether it is shown. */
static void
test_cursor_place(void)
{
	struct amdrdna_cursor_place place;

	/* Inside the plane: the corner is the position, no hot spot. */
	amdrdna_cursor_locate(928, 508, 64U, 1920U, 1080U, &place);
	check(place.shown && place.position == ((928U << 16) | 508U) && place.hot_spot == 0U,
	    "cursor.inside", "the corner is not the position");

	/* Over the left edge: x zero, the overhang as the hot spot. */
	amdrdna_cursor_locate(-10, 20, 64U, 1920U, 1080U, &place);
	check(place.shown && place.position == 20U && place.hot_spot == (10U << 16),
	    "cursor.left", "the left overhang is not the hot spot");

	/* Over the top edge. */
	amdrdna_cursor_locate(30, -63, 64U, 1920U, 1080U, &place);
	check(place.shown && place.position == (30U << 16) && place.hot_spot == 63U,
	    "cursor.top", "the top overhang is not the hot spot");

	/* Over both, by the largest overhang that still shows a pixel. */
	amdrdna_cursor_locate(-63, -63, 64U, 1920U, 1080U, &place);
	check(place.shown && place.position == 0U && place.hot_spot == ((63U << 16) | 63U),
	    "cursor.corner", "the corner overhang is wrong");

	/* Wholly outside each edge. */
	amdrdna_cursor_locate(-64, 0, 64U, 1920U, 1080U, &place);
	check(!place.shown, "cursor.off-left", "an image left of the plane is shown");
	amdrdna_cursor_locate(0, -64, 64U, 1920U, 1080U, &place);
	check(!place.shown, "cursor.off-top", "an image above the plane is shown");
	amdrdna_cursor_locate(1920, 0, 64U, 1920U, 1080U, &place);
	check(!place.shown, "cursor.off-right", "an image right of the plane is shown");
	amdrdna_cursor_locate(0, 1080, 64U, 1920U, 1080U, &place);
	check(!place.shown, "cursor.off-bottom", "an image below the plane is shown");

	/* The last pixel column and row still show it. */
	amdrdna_cursor_locate(1919, 1079, 64U, 1920U, 1080U, &place);
	check(place.shown && place.position == ((1919U << 16) | 1079U),
	    "cursor.last-pixel", "an image on the last pixel is hidden");

	/* The far corners of a 32-bit position do not overflow. */
	amdrdna_cursor_locate(INT32_MIN, INT32_MAX, 64U, 1920U, 1080U, &place);
	check(!place.shown, "cursor.extreme", "an extreme position is shown");
}

/* The cursor's control, size and timing words, and the pixel processor's control word. */
static void
test_cursor_words(void)
{
	uint32_t word;

	/*
	 * The plane's word, shown: enable 0x1, mode 2 (premultiplied) at
	 * 10:8, pitch 0 (64) at 17:16, eight lines (3) at 28:24; the other
	 * bits as read (here the snoop bit 0x2000 and the rotation bypass
	 * 0x100000), and the magnify, protected, system and old mode, pitch
	 * and lines bits cleared.
	 */
	word = amdrdna_cursor_control_word(0x1f137711U | 0x00002000U | 0x00100000U, true);
	check(word == (0x00000201U | 0x03000000U | 0x00002000U | 0x00100000U),
	    "cursor.control-shown", "the shown control word is wrong");

	/* Hidden: the same without the enable bit. */
	word = amdrdna_cursor_control_word(0x00000001U, false);
	check(word == 0x03000200U, "cursor.control-hidden", "the hidden control word is wrong");

	/* The size: width at 24:16, height at 8:0. */
	check(amdrdna_cursor_size_word(64U) == 0x00400040U, "cursor.size", "the size word is wrong");

	/* The timing: no shift, the deadline moved by three at 9:8. */
	check(amdrdna_cursor_timing_word() == 0x00000300U, "cursor.timing", "the timing word is wrong");

	/*
	 * The pixel processor's word: enable 0x1, mode 2 at 6:4, expansion
	 * 0x2 and degamma 0x8 cleared, the inversion bit 0x4 and the
	 * alpha-modulate bit 0x80 as read.
	 */
	word = amdrdna_pixel_cursor_word(0x000000ffU, true);
	check(word == 0x000000a5U, "cursor.pixel-shown", "the pixel processor's shown word is wrong");
	word = amdrdna_pixel_cursor_word(0x00000021U, false);
	check(word == 0x00000020U, "cursor.pixel-hidden", "the pixel processor's hidden word is wrong");
}

/* The frame count's advance across its 24 bits and the refresh from counted frames. */
static void
test_frames(void)
{
	/* A plain advance and one across the wrap of the 24 bits. */
	check(amdrdna_frame_advance(10U, 70U) == 60U, "frames.advance", "a plain advance is wrong");
	check(amdrdna_frame_advance(0x00fffff0U, 0x00000010U) == 0x20U, "frames.wrap", "the advance across the wrap is wrong");
	check(amdrdna_frame_advance(0x12345678U, 0x12345678U) == 0U, "frames.none", "no advance is not zero");

	/* 60 frames in a second, 30 in half a second, and 59.94 Hz. */
	check(amdrdna_refresh_millihertz(60U, 1000000U) == 60000U, "refresh.60", "60 frames a second is not 60 Hz");
	check(amdrdna_refresh_millihertz(30U, 500000U) == 60000U, "refresh.half", "30 frames in half a second is not 60 Hz");
	check(amdrdna_refresh_millihertz(30U, 500500U) == 59940U, "refresh.ntsc", "30 frames in 500.5 ms is not 59.940 Hz");
	check(amdrdna_refresh_millihertz(30U, 0U) == 0U, "refresh.no-time", "no time gives a refresh");
}

/* The interrupt words: acknowledgements only when asked, the other bits as read. */
static void
test_interrupt_words(void)
{
	uint32_t word;

	/*
	 * The sync status: every acknowledgement (0x10, 0x400, 0x10000,
	 * 0x400000) read back is not written; the frame interrupt 0x1 on and
	 * its acknowledgement 0x10 when asked; the other enables (0x20, 0x1000)
	 * as read.
	 */
	word = amdrdna_sync_status_word(0x00411431U, true, false);
	check(word == 0x00001021U, "sync.enable", "the frame interrupt's enable word is wrong");
	word = amdrdna_sync_status_word(0x00001021U, true, true);
	check(word == 0x00001031U, "sync.acknowledge", "the frame interrupt's acknowledgement is wrong");
	word = amdrdna_sync_status_word(0x00001021U, false, true);
	check(word == 0x00001030U, "sync.off", "the frame interrupt is not off");

	/* The flip interrupt: mask 0x1, acknowledgement 0x100; the flip-away acknowledgement 0x200 is never written. */
	word = amdrdna_flip_interrupt_word(0x00030302U, true, true);
	check(word == 0x00030103U, "flip.acknowledge", "the flip interrupt's word is wrong");
	word = amdrdna_flip_interrupt_word(0x00000103U, false, false);
	check(word == 0x00000002U, "flip.off", "the flip interrupt is not off");

	/* The flip control: the at-once bit 0x2 cleared, the rest as read; bit 8 is pending. */
	check(amdrdna_flip_control_word(0x00000013U) == 0x00000011U, "flip.control", "the flip control word is wrong");
	check(amdrdna_flip_pending(0x00000100U), "flip.pending", "a pending flip is not seen");
	check(!amdrdna_flip_pending(0x00000200U), "flip.not-pending", "a lock status is taken for a pending flip");

	/*
	 * The hot-plug control: enable 0x10000, the level that interrupts
	 * 0x100 set while disconnected, acknowledgements 0x1 and 0x100000
	 * only when asked, the short-pulse enable 0x1000000 as read.
	 */
	word = amdrdna_hotplug_control_word(0x01100101U, true, true, true, false);
	check(word == 0x01010001U, "hotplug.connected", "the word of a connected line is wrong");
	word = amdrdna_hotplug_control_word(0x00010000U, true, false, false, true);
	check(word == 0x00110100U, "hotplug.disconnected", "the word of a disconnected line is wrong");
	word = amdrdna_hotplug_control_word(0x00010100U, false, true, false, false);
	check(word == 0x00000000U, "hotplug.off", "the line's interrupt is not off");

	/* The debounced level 0x10 says a monitor is connected; the raw level 0x2 alone does not. */
	check(amdrdna_hotplug_connected(0x00000012U), "hotplug.level", "a connected line is not seen");
	check(!amdrdna_hotplug_connected(0x00000002U), "hotplug.raw", "the raw level alone is taken for a connection");
}
