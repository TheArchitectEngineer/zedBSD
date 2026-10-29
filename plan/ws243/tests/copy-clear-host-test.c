/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the copy engine's clear of the VRAM (the push buffer the
 * driver's work channel runs for a new blob): the constant and the
 * components, the destination, the rows' pitch, length and count, and the
 * multi-line launch with the remap.
 *
 * Field positions referenced: NVIDIA open-gpu-doc classes/dma-copy/clc5b5.h
 * (NVC5B5_LAUNCH_DMA, NVC5B5_PITCH_OUT, NVC5B5_LINE_LENGTH_IN,
 * NVC5B5_LINE_COUNT, NVC5B5_SET_REMAP_COMPONENTS) and classes/host/clc46f.h
 * (the incrementing method header).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

/* How many checks the run has made; it only grows, and the summary reports it. */
static int checks;

/* How many of the checks failed; zero means the run passed. */
static int failures;

static void check(int ok, const char *what);
static uint32_t header(uint32_t subchannel, uint32_t method, uint32_t count);

int
main(void)
{
	uint8_t push[2U * NVRTX_PUSH_COPY_CLEAR_BYTES];
	size_t length;
	size_t second;

	/* An 8 MiB run: 64 rows of 32768 words on subchannel 4, then a 1 KiB rest in one row. */
	memset(push, 0xa5, sizeof(push));
	length = nvrtx_push_copy_clear(push, 4U, 0x120340000ULL, 0x8000U, 64U, 0U);
	check(length == NVRTX_PUSH_COPY_CLEAR_BYTES, "rows: length");
	check(nvrtx_get32(push, 0) == header(4U, 0x700U, 1U) && nvrtx_get32(push, 4) == 0U, "rows: constant zero");
	check(nvrtx_get32(push, 8) == header(4U, 0x708U, 1U) && nvrtx_get32(push, 12) == (4U | (3U << 16)),
	    "rows: X from constant A, 4-byte components, one each");
	check(nvrtx_get32(push, 16) == header(4U, 0x408U, 6U) && nvrtx_get32(push, 20) == 0x1U &&
	    nvrtx_get32(push, 24) == 0x20340000U, "rows: destination");
	check(nvrtx_get32(push, 28) == 0x20000U && nvrtx_get32(push, 32) == 0x20000U &&
	    nvrtx_get32(push, 36) == 0x8000U && nvrtx_get32(push, 40) == 64U, "rows: pitches 128 KiB, 32768 words, 64 rows");
	check(nvrtx_get32(push, 44) == header(4U, 0x300U, 1U) &&
	    nvrtx_get32(push, 48) == (2U | 4U | 0x80U | 0x100U | 0x200U | 0x400U),
	    "rows: launch non-pipelined, flushed, pitch to pitch, multi-line, remapped");

	/* The rest after the rows. */
	second = nvrtx_push_copy_clear(push + length, 4U, 0x120340000ULL + 64U * 0x20000U, 256U, 1U, 0U);
	check(second == NVRTX_PUSH_COPY_CLEAR_BYTES, "rest: length");
	check(nvrtx_get32(push, length + 24U) == 0x20b40000U && nvrtx_get32(push, length + 32U) == 1024U &&
	    nvrtx_get32(push, length + 36U) == 256U && nvrtx_get32(push, length + 40U) == 1U, "rest: one row of 256 words after the rows");

	/* The summary. */
	printf("copy clear host test: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

/* Counts a check and reports one that failed. */
static void
check(
	int ok,
	const char *what)
{
	/* Counts the check, so a run that skipped checks shows in the total. */
	checks++;

	/* Reports a check that failed, and counts it. */
	if (!ok) {
		failures++;
		printf("FAIL %s\n", what);
	}
}

/* An incrementing method header: the count, the subchannel and the method's word address. */
static uint32_t
header(
	uint32_t subchannel,
	uint32_t method,
	uint32_t count)
{
	/* Succeeded: the header's fields. */
	return (1U << 29) | (count << 16) | (subchannel << 13) | (method >> 2);
}
