/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws207-p002: the headphone jack's arithmetic on the host
 * (src/drivers/platform/rpi4/rpi4-audio-ring.c): samples to duty words,
 * the channel order, the DMA's ring of control blocks, and the fragment
 * and position the DMA's source address gives.
 */

#include "drivers/platform/rpi4/rpi4-audio-ring.h"

#include <stdio.h>
#include <stdlib.h>

/* The checks that failed. */
static unsigned failures;

static void check(int good, const char *what);
static void test_duty(void);
static void test_convert(void);
static void test_chain(void);
static void test_fragment(void);

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	/* Reports a failure and counts it. */
	if (!good) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* The duty of the extremes and the middle at 48 kHz on 54 MHz (1125 clocks). */
static void
test_duty(
	void)
{
	uint32_t last;
	uint32_t duty;
	int sample;

	/* The ends and the middle. */
	check(rpi4_audio_duty(-32768, 1125U) == 0U, "duty of -32768");
	check(rpi4_audio_duty(32767, 1125U) == 1124U, "duty of 32767");
	check(rpi4_audio_duty(0, 1125U) == 562U, "duty of 0");

	/* Monotonic and under the range across every sample. */
	last = 0U;
	for (sample = -32768; sample <= 32767; sample++) {
		duty = rpi4_audio_duty((int16_t)sample, 1224U);
		if (duty < last || duty >= 1224U) {
			check(0, "duty monotonic and below the range");
			return;
		}

		/* The next sample must not fall below this one. */
		last = duty;
	}

	/* Every sample was checked. */
	return;
}

/* Left and right into channel 1 and 2, either way round. */
static void
test_convert(
	void)
{
	static const int16_t pcm[4] = { -32768, 32767, 0, -32768 };
	uint32_t words[4];

	/* Left on channel 1. */
	rpi4_audio_convert(pcm, words, 2U, 1125U, 0);
	check(words[0] == 0U && words[1] == 1124U && words[2] == 562U && words[3] == 0U, "convert left first");

	/* Right on channel 1. */
	rpi4_audio_convert(pcm, words, 2U, 1125U, 1);
	check(words[0] == 1124U && words[1] == 0U && words[2] == 0U && words[3] == 562U, "convert right first");
}

/* Eight blocks of 8 KiB from 0xc0100100, control blocks at 0xc0100000. */
static void
test_chain(
	void)
{
	struct rpi4_dma_cb cbs[8];
	unsigned index;
	int good;

	/* The ring. */
	rpi4_audio_chain(cbs, 8U, 0xc0100000U, 0xc0100100U, 8192U, 0x7e20c818U);
	good = 1;
	for (index = 0U; index < 8U; index++) {
		if (cbs[index].ti != 0x00010149U || cbs[index].source != 0xc0100100U + index * 8192U ||
		    cbs[index].destination != 0x7e20c818U || cbs[index].length != 8192U || cbs[index].stride != 0U ||
		    cbs[index].reserved[0] != 0U || cbs[index].reserved[1] != 0U)
			good = 0;
		if (cbs[index].next != 0xc0100000U + ((index + 1U) % 8U) * 32U)
			good = 0;
	}

	/* The whole ring and the block's size. */
	check(good, "chain of eight blocks");
	check(sizeof(struct rpi4_dma_cb) == 32U, "control block of 32 bytes");
}

/* The fragment and position of source addresses. */
static void
test_fragment(
	void)
{
	/* Inside, at block starts, at the end and outside. */
	check(rpi4_audio_fragment(0xc0100100U, 0xc0100100U, 8192U, 8U) == 0U, "fragment at the start");
	check(rpi4_audio_fragment(0xc0100100U + 8191U, 0xc0100100U, 8192U, 8U) == 0U, "fragment at a block's last byte");
	check(rpi4_audio_fragment(0xc0100100U + 8192U * 3U, 0xc0100100U, 8192U, 8U) == 3U, "fragment 3");
	check(rpi4_audio_fragment(0xc0100100U + 8192U * 8U, 0xc0100100U, 8192U, 8U) == 0U, "fragment at the end wraps");
	check(rpi4_audio_fragment(0xc0100100U + 8192U * 8U + 4U, 0xc0100100U, 8192U, 8U) == 8U, "fragment beyond");
	check(rpi4_audio_fragment(0xc01000fcU, 0xc0100100U, 8192U, 8U) == 8U, "fragment before");

	/* Positions in sample bytes. */
	check(rpi4_audio_position(0xc0100100U + 8U, 0xc0100100U, 8192U, 8U) == 4U, "position of one frame");
	check(rpi4_audio_position(0xc0100100U + 8192U * 5U + 16U, 0xc0100100U, 8192U, 8U) == 4096U * 5U + 8U,
	      "position in block 5");
	check(rpi4_audio_position(0xc0100100U + 8192U * 8U, 0xc0100100U, 8192U, 8U) == 0U, "position at the end wraps");
	check(rpi4_audio_position(0x7e20c818U, 0xc0100100U, 8192U, 8U) == 0U, "position outside");
}

/* Runs every check. */
int
main(
	void)
{
	/* Each group. */
	test_duty();
	test_convert();
	test_chain();
	test_fragment();

	/* The summary. */
	if (failures != 0U) {
		printf("rpi4-audio-ring: %u FAIL\n", failures);
		return EXIT_FAILURE;
	}

	/* Succeeded: every check passed. */
	printf("rpi4-audio-ring: all PASS\n");
	return EXIT_SUCCESS;
}
