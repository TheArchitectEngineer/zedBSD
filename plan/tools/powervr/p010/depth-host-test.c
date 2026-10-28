/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269-p010j (render-depth.c): the depth load and store
 * words of D32_SFLOAT and D16_UNORM targets (cleared and stored, loaded,
 * a partial area that loads a stored depth, none), against golden values
 * the one-time M9 comparison with Mesa 26.2.5's csbgen packing confirmed
 * (phase010j/phase.md); the 16-bit clear value's rounding; and the
 * refusals.  -v prints the words.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-depth.h"

static unsigned passed;
static unsigned failed;

static void
check(
	int ok,
	const char *name)
{
	if (ok) {
		passed++;
	} else {
		failed++;
		printf("FAIL %s\n", name);
	}
}

/* A 64 x 64 twiddled depth of a format at 0x100000, cleared and stored. */
static void
depth_init(
	struct powervr_render_depth *depth,
	uint32_t format)
{
	memset(depth, 0, sizeof(*depth));
	depth->present = 1;
	depth->format = powervr_format_find(format);
	depth->address = 0x100000;
	depth->width = 64;
	depth->height = 64;
	depth->physical_width = 64;
	depth->physical_height = 64;
	depth->twiddled = 1;
	depth->layer_bytes = 0x4000;
	depth->load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth->store_op = VK_ATTACHMENT_STORE_OP_STORE;
	depth->clear_depth = 0x3f000000U;
}

int
main(
	int argc,
	char **argv)
{
	struct powervr_render_depth depth;
	struct powervr_render_zls zls;
	int verbose;
	int ok;

	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

	/* D32, cleared to 0.5 and stored: store only, twiddled, four tiles each way less one. */
	depth_init(&depth, VK_FORMAT_D32_SFLOAT);
	ok = powervr_render_zls_words(&depth, &zls) == 0;
	if (verbose)
		printf("d32 store: %016llx base %llx depth %08x stride %x\n", (unsigned long long)zls.control, (unsigned long long)zls.base, zls.background_depth, zls.stride);
	check(ok && zls.control == 0x00000000180a201cULL && zls.base == 0x100000U && zls.background_depth == 0x3f000000U && zls.stride == 0x4000U && zls.process_empty_tiles, "d32 cleared and stored");

	/* D32 loaded, not stored. */
	depth.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ok = powervr_render_zls_words(&depth, &zls) == 0;
	if (verbose)
		printf("d32 load: %016llx\n", (unsigned long long)zls.control);
	check(ok && zls.control == 0x000000001802a01aULL, "d32 loaded");

	/* A partial area with a store: loaded and stored. */
	depth.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.store_op = VK_ATTACHMENT_STORE_OP_STORE;
	depth.partial = 1;
	ok = powervr_render_zls_words(&depth, &zls) == 0;
	check(ok && (zls.control & (1ULL << 15)) != 0 && (zls.control & (1ULL << 19)) != 0, "partial area loads a stored depth");

	/* D16, cleared to 0.5 (32768 rounded to even), 320 x 240 over a 512 x 256 buffer. */
	depth_init(&depth, VK_FORMAT_D16_UNORM);
	depth.width = 320;
	depth.height = 240;
	depth.physical_width = 512;
	depth.physical_height = 256;
	ok = powervr_render_zls_words(&depth, &zls) == 0;
	if (verbose)
		printf("d16 store: %016llx depth %08x\n", (unsigned long long)zls.control, zls.background_depth);
	check(ok && zls.background_depth == 32768U && ((zls.control >> 23) & 3U) == 2U && ((zls.control >> 3) & 0x3ffU) == 31U && ((zls.control >> 27) & 0x3ffU) == 15U, "d16 cleared and stored");

	/* 16-bit clear values: 0, 1, 0.25, a value just below one unit's half. */
	depth.clear_depth = 0;
	check(powervr_render_zls_words(&depth, &zls) == 0 && zls.background_depth == 0, "d16 clear 0");
	depth.clear_depth = 0x3f800000U;
	check(powervr_render_zls_words(&depth, &zls) == 0 && zls.background_depth == 65535U, "d16 clear 1");
	depth.clear_depth = 0x3e800000U;
	check(powervr_render_zls_words(&depth, &zls) == 0 && zls.background_depth == 16384U, "d16 clear 0.25 (16383.75 rounds up)");

	/* No depth: nothing loaded or stored, the background 1.0. */
	memset(&depth, 0, sizeof(depth));
	ok = powervr_render_zls_words(&depth, &zls) == 0;
	check(ok && zls.control == 0 && zls.background_depth == 0x3f800000U && !zls.process_empty_tiles, "no depth");

	/* Neither loaded nor stored: no empty tiles to process. */
	depth_init(&depth, VK_FORMAT_D32_SFLOAT);
	depth.store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ok = powervr_render_zls_words(&depth, &zls) == 0;
	check(ok && !zls.process_empty_tiles && (zls.control & ((1ULL << 15) | (1ULL << 19))) == 0, "a cleared depth not stored");

	/* Refusals: a buffer below a ZLS tile that is stored, a colour format, a misaligned address. */
	depth_init(&depth, VK_FORMAT_D32_SFLOAT);
	depth.physical_width = 16;
	check(powervr_render_zls_words(&depth, &zls) == ENOTSUP, "a small stored buffer refused");
	depth_init(&depth, VK_FORMAT_D32_SFLOAT);
	depth.format = powervr_format_find(VK_FORMAT_R8G8B8A8_UNORM);
	check(powervr_render_zls_words(&depth, &zls) == ENOTSUP, "a colour format refused");
	depth_init(&depth, VK_FORMAT_D32_SFLOAT);
	depth.address = 0x100008;
	check(powervr_render_zls_words(&depth, &zls) == EINVAL, "a misaligned address refused");

	printf("render-depth host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
