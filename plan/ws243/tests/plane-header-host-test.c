/*
 * Host test of the texture headers of a video picture's planes: the luma
 * plane as R8 and the chroma plane as R8G8, block linear in blocks two
 * GOBs high, at the planes' places of the NV12 layout the decoder writes.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * Field positions referenced: Mesa 25.0.7
 * src/nouveau/headers/nvidia/classes/clb097tex.h (NVB097_TEXHEAD_BL_*).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libc/vulkan/vulkan_core.h>

#include "drivers/gpu/nvrtx/vk/nvrtx-exec.h"
#include "drivers/gpu/nvrtx/vk/nvrtx-video.h"

static int checks;
static int failures;

static void check(int ok, const char *what);
static uint32_t field(const uint32_t *words, unsigned low, unsigned bits);
static void test_plane(uint32_t width, uint32_t height);
static void test_swizzle(void);

int
main(void)
{
	test_plane(1920U, 1088U);
	test_plane(1280U, 720U);
	test_plane(720U, 480U);
	test_swizzle();
	printf("plane header host test: %d checks, %d failed\n", checks, failures);
	if (failures != 0)
		return 1;
	return 0;
}

/* Counts one check and reports it when it fails. */
static void
check(
	int ok,
	const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL %s\n", what);
	}
}

/* Reads a field of bits bits from bit low, counted over the words. */
static uint32_t
field(
	const uint32_t *words,
	unsigned low,
	unsigned bits)
{
	uint32_t value;
	unsigned bit;
	unsigned position;

	value = 0U;
	for (bit = 0U; bit < bits; bit++) {
		position = low + bit;
		if (((words[position / 32U] >> (position % 32U)) & 1U) != 0U)
			value |= 1U << bit;
	}
	return value;
}

/* Both planes of a picture of width by height at an image address: their places, formats, sizes and blocks. */
static void
test_plane(
	uint32_t width,
	uint32_t height)
{
	static const uint32_t identity[4] = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
					      VK_COMPONENT_SWIZZLE_A };
	struct nvrtx_video_layout layout;
	uint32_t luma[8];
	uint32_t chroma[8];
	uint64_t image;
	uint64_t address;
	char what[96];

	image = 0x1234560000ULL;
	nvrtx_video_layout(width, height, &layout);
	nvrtx_exec_texture_header_plane(luma, image + layout.plane_offset[0], width, height, 1U, identity);
	nvrtx_exec_texture_header_plane(chroma, image + layout.plane_offset[1], width / 2U, height / 2U, 2U, identity);

	/* The chroma plane's place takes a block-linear header's 512-byte address. */
	snprintf(what, sizeof(what), "%ux%u: chroma plane on 512 bytes", width, height);
	check((layout.plane_offset[1] & 511U) == 0U, what);

	/* The component sizes: R8, then G8R8; every channel unsigned normalized. */
	snprintf(what, sizeof(what), "%ux%u: R8 and G8R8", width, height);
	check(field(luma, 0U, 7U) == 0x1dU && field(chroma, 0U, 7U) == 0x18U, what);
	snprintf(what, sizeof(what), "%ux%u: unorm channels", width, height);
	check(field(luma, 7U, 3U) == 2U && field(luma, 16U, 3U) == 2U && field(chroma, 10U, 3U) == 2U, what);

	/* The sources: red; red and green; the others zero and alpha one. */
	snprintf(what, sizeof(what), "%ux%u: luma sources R 0 0 1", width, height);
	check(field(luma, 19U, 3U) == 2U && field(luma, 22U, 3U) == 0U && field(luma, 25U, 3U) == 0U &&
	      field(luma, 28U, 3U) == 7U, what);
	snprintf(what, sizeof(what), "%ux%u: chroma sources R G 0 1", width, height);
	check(field(chroma, 19U, 3U) == 2U && field(chroma, 22U, 3U) == 3U && field(chroma, 25U, 3U) == 0U &&
	      field(chroma, 28U, 3U) == 7U, what);

	/* The addresses (bits 31:9 and 47:32), block linear. */
	address = image + layout.plane_offset[0];
	snprintf(what, sizeof(what), "%ux%u: luma address", width, height);
	check(field(luma, 41U, 23U) == (uint32_t)((address & 0xffffffffU) >> 9) && field(luma, 64U, 16U) == (uint32_t)(address >> 32),
	      what);
	address = image + layout.plane_offset[1];
	snprintf(what, sizeof(what), "%ux%u: chroma address", width, height);
	check(field(chroma, 41U, 23U) == (uint32_t)((address & 0xffffffffU) >> 9) &&
	      field(chroma, 64U, 16U) == (uint32_t)(address >> 32), what);
	snprintf(what, sizeof(what), "%ux%u: block linear header", width, height);
	check(field(luma, 85U, 3U) == 3U && field(chroma, 85U, 3U) == 3U, what);

	/* Blocks one GOB wide and two high, one GOB deep. */
	snprintf(what, sizeof(what), "%ux%u: blocks 1x2x1 GOBs", width, height);
	check(field(luma, 96U, 3U) == 0U && field(luma, 99U, 3U) == 1U && field(luma, 102U, 3U) == 0U &&
	      field(chroma, 96U, 3U) == 0U && field(chroma, 99U, 3U) == 1U && field(chroma, 102U, 3U) == 0U, what);

	/* The sizes, 2D, one level. */
	snprintf(what, sizeof(what), "%ux%u: luma size", width, height);
	check(field(luma, 128U, 16U) == width - 1U && field(luma, 160U, 16U) == height - 1U, what);
	snprintf(what, sizeof(what), "%ux%u: chroma size", width, height);
	check(field(chroma, 128U, 16U) == width / 2U - 1U && field(chroma, 160U, 16U) == height / 2U - 1U, what);
	snprintf(what, sizeof(what), "%ux%u: 2D of one level", width, height);
	check(field(luma, 151U, 4U) == 1U && field(luma, 124U, 4U) == 0U && field(luma, 224U, 4U) == 0U &&
	      field(luma, 228U, 4U) == 0U, what);

	/* A row of blocks is the decoder's pitch: the luma width's bytes, the chroma pairs' bytes, in whole GOBs. */
	snprintf(what, sizeof(what), "%ux%u: rows of the decoder's pitch", width, height);
	check(((width + 63U) & ~63U) == layout.pitch && (((width / 2U) * 2U + 63U) & ~63U) == layout.pitch, what);
}

/* A view's swizzle moves the plane's channels: the chroma plane's green as red. */
static void
test_swizzle(void)
{
	static const uint32_t swapped[4] = { VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE,
					     VK_COMPONENT_SWIZZLE_ZERO };
	uint32_t header[8];

	nvrtx_exec_texture_header_plane(header, 0x200000ULL, 960U, 544U, 2U, swapped);
	check(field(header, 19U, 3U) == 3U && field(header, 22U, 3U) == 2U && field(header, 25U, 3U) == 7U &&
	      field(header, 28U, 3U) == 0U, "swizzle: G R 1 0");
}
