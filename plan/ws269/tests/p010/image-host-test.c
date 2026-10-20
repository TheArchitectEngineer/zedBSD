/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of ws269-p010c: image-state.c's format table, the layout of
 * linear and twiddled images, the words of views and samplers, and the
 * sizes of descriptors.  The expected words were compared once with the
 * packers that Mesa 26.2.5's csbgen/gen_pack_header.py makes of
 * texstate.xml (plan/ws269/phase010c/phase.md, the M9 check).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/image-state.h"

/* What RGBA8 reports on linear images before the shared path. */
#define FEATURES_LINEAR_RGBA	(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | \
				 VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | \
				 VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)

static int failures;
static int passes;

/* One expectation. */
static void
expect(
	int good,
	const char *what)
{
	if (good) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}

/* A float's bits. */
static uint32_t
bits(
	float value)
{
	uint32_t word;

	memcpy(&word, &value, sizeof(word));
	return word;
}

/* Lays an image out. */
static int
lay(
	uint32_t vk_format,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levels,
	uint32_t layers,
	int linear,
	int volume,
	struct powervr_image_layout *layout)
{
	struct powervr_image_shape shape;

	memset(&shape, 0, sizeof(shape));
	shape.width = width;
	shape.height = height;
	shape.depth = depth;
	shape.levels = levels;
	shape.layers = layers;
	shape.linear = linear != 0;
	shape.volume = volume != 0;
	return powervr_image_lay_out(&shape, powervr_format_find(vk_format), layout);
}

/* The words of a view of a whole image, with the identity mapping unless map is given. */
static int
view(
	uint32_t vk_format,
	const struct powervr_image_layout *layout,
	uint32_t width,
	uint32_t height,
	uint32_t levels,
	uint32_t base_level,
	uint32_t level_count,
	uint32_t base_layer,
	uint32_t layer_count,
	int linear,
	const uint32_t *map,
	uint64_t address,
	uint64_t words[2])
{
	struct powervr_view_shape shape;

	memset(&shape, 0, sizeof(shape));
	shape.format = powervr_format_find(vk_format);
	shape.layout = layout;
	shape.address = address;
	shape.width = width;
	shape.height = height;
	shape.image_levels = levels;
	shape.base_level = base_level;
	shape.level_count = level_count;
	shape.base_layer = base_layer;
	shape.layer_count = layer_count;
	shape.linear = linear != 0;
	if (map != NULL)
		memcpy(shape.mapping, map, sizeof(shape.mapping));
	return powervr_view_words(&shape, words);
}

int
main(void)
{
	static const uint32_t formats[] = {
		VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB,
		VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT
	};
	static const uint32_t odd[4] = { VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE };
	static const uint32_t widths[] = { 1, 2, 3, 7, 8, 9, 31, 33, 100, 255, 320, 1366, 1920, 4096 };
	struct powervr_image_layout layout;
	struct powervr_sampler_shape sampler;
	const struct powervr_format *format;
	uint64_t words[2];
	uint32_t bytes;
	unsigned i;
	unsigned j;
	int pitch_good;
	int error;

	/* The table: every format found, none with linear features yet, an unknown one absent. */
	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		format = powervr_format_find(formats[i]);
		expect(format != NULL, "a format of the table is found");
		if (format != NULL)
			expect((powervr_format_linear_features(format) & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0, "no linear sampling before the shared path");
	}
	expect(powervr_format_find(VK_FORMAT_R16_UNORM) == NULL, "a format outside the table is absent");
	expect(powervr_format_clear_word_count(powervr_format_find(VK_FORMAT_B8G8R8A8_UNORM)) == 1 && powervr_format_clear_word_count(powervr_format_find(VK_FORMAT_B8G8R8A8_SRGB)) == 2 &&
	    powervr_format_clear_word_count(powervr_format_find(VK_FORMAT_R32G32B32A32_SFLOAT)) == 4 && powervr_format_clear_word_count(powervr_format_find(VK_FORMAT_D32_SFLOAT)) == 0, "clear word counts: 1 for RGBA8, 2 for sRGB, 4 for RGBA32F, none for depth");
	format = powervr_format_find(VK_FORMAT_R8G8B8A8_UNORM);
	expect(format != NULL && (format->optimal_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0 && (format->optimal_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0, "RGBA8 optimal draws and filters");
	expect(format != NULL && powervr_format_linear_features(format) == (FEATURES_LINEAR_RGBA), "RGBA8 linear: all but sampling");
	format = powervr_format_find(VK_FORMAT_D32_SFLOAT);
	expect(format != NULL && format->optimal_features == (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT) && format->linear_features == 0, "D32 depth and copies, optimal only");
	format = powervr_format_find(VK_FORMAT_R32G32B32_SFLOAT);
	expect(format != NULL && format->buffer_features == VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT && format->optimal_features == 0, "RGB32F a vertex format only");

	/* Linear rows: the fewest texels whose bytes are a multiple of 32 and whose count is even. */
	pitch_good = 1;
	for (i = 0; i < 3; i++) {
		static const uint32_t linear_formats[] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
		bytes = powervr_format_find(linear_formats[i])->texel_bytes;
		for (j = 0; j < sizeof(widths) / sizeof(widths[0]); j++) {
			error = lay(linear_formats[i], widths[j], 3, 1, 1, 1, 1, 0, &layout);
			if (error != 0 ||
			    layout.row_pitch % 32U != 0 ||
			    (layout.row_pitch / bytes) % 2U != 0 ||
			    layout.row_pitch < widths[j] * bytes ||
			    layout.row_pitch - 32U >= widths[j] * bytes ||
			    layout.padded_width * bytes != layout.row_pitch ||
			    layout.bytes != (uint64_t)layout.row_pitch * 3U) {
				pitch_good = 0;
				printf("  linear format %u width %u: pitch %u\n", linear_formats[i], widths[j], layout.row_pitch);
			}
		}
	}
	expect(pitch_good, "linear rows are the smallest multiple of 32 bytes and two texels");
	error = lay(VK_FORMAT_B8G8R8A8_UNORM, 1920, 1080, 1, 1, 1, 1, 0, &layout);
	expect(error == 0 && layout.row_pitch == 7680 && layout.bytes == 7680U * 1080U, "1920 BGRA linear rows of 7680 bytes");
	error = lay(VK_FORMAT_B8G8R8A8_UNORM, 64, 64, 1, 2, 1, 1, 0, &layout);
	expect(error == ENOTSUP, "a linear image of two levels is not taken");
	error = lay(VK_FORMAT_B8G8R8A8_UNORM, 64, 64, 1, 1, 2, 1, 0, &layout);
	expect(error == ENOTSUP, "a linear image of two layers is not taken");

	/* Twiddled: edges to powers of two, levels in turn, the layer counted towards one texel. */
	error = lay(VK_FORMAT_R8G8B8A8_UNORM, 320, 240, 1, 1, 1, 0, 0, &layout);
	expect(error == 0 && layout.padded_width == 512 && layout.padded_height == 256 && layout.bytes == 512U * 256U * 4U, "320x240 is 512x256 twiddled");
	error = lay(VK_FORMAT_R8G8B8A8_UNORM, 64, 32, 1, 7, 1, 0, 0, &layout);
	expect(error == 0 && layout.bytes == 10924 && layout.level_offset[1] == 8192 && layout.level_offset[6] == 10920, "a whole chain of 64x32");
	error = lay(VK_FORMAT_R8G8B8A8_UNORM, 64, 32, 1, 3, 4, 0, 0, &layout);
	expect(error == 0 && layout.layer_bytes == 12288 && layout.bytes == 49152 && layout.level_offset[2] == 10240, "three levels of four layers: the layer holds the rest of the chain, on the page");
	error = lay(VK_FORMAT_R8G8B8A8_UNORM, 5, 3, 2, 1, 1, 0, 1, &layout);
	expect(error == 0 && layout.padded_depth == 2 && layout.bytes == 8U * 4U * 4U * 4U, "a volume's edges aligned to four after the powers of two");
	error = lay(VK_FORMAT_R8G8B8A8_UNORM, 64, 32, 1, 8, 1, 0, 0, &layout);
	expect(error == EINVAL, "more levels than the edges halve is refused");
	error = lay(VK_FORMAT_R8G8B8A8_UNORM, 0, 32, 1, 1, 1, 0, 0, &layout);
	expect(error == EINVAL, "an empty edge is refused");
	error = lay(VK_FORMAT_R32G32B32_SFLOAT, 16, 16, 1, 1, 1, 0, 0, &layout);
	expect(error == ENOTSUP, "a vertex-only format makes no image");

	/* The words of views (compared once with Mesa's packers). */
	lay(VK_FORMAT_R8G8B8A8_UNORM, 64, 32, 1, 7, 4, 0, 0, &layout);
	error = view(VK_FORMAT_R8G8B8A8_UNORM, &layout, 64, 32, 7, 2, 3, 1, 2, 0, NULL, 0x8000001000ULL, words);
	expect(error == 0 && words[0] == 0x001f00fc60000a61ULL && words[1] == 0x2020000010008013ULL, "twiddled RGBA8 view of levels 2-4, layers 1-2");
	lay(VK_FORMAT_B8G8R8A8_SRGB, 64, 32, 1, 7, 1, 0, 0, &layout);
	error = view(VK_FORMAT_B8G8R8A8_SRGB, &layout, 64, 32, 7, 0, 7, 0, 1, 0, NULL, 0x8000001000ULL, words);
	expect(error == 0 && words[0] == 0x001f00fc60008869ULL && words[1] == 0x0020000004008007ULL, "twiddled BGRA8 sRGB view: swapped channels and gamma");
	lay(VK_FORMAT_D32_SFLOAT, 16, 16, 1, 1, 1, 0, 0, &layout);
	error = view(VK_FORMAT_D32_SFLOAT, &layout, 16, 16, 1, 0, 1, 0, 1, 0, odd, 0x8000002000ULL, words);
	expect(error == 0 && words[0] == 0x000f003c90016881ULL && words[1] == 0x0020000008000001ULL, "a depth view with a mapping: absent channels read zero");
	lay(VK_FORMAT_B8G8R8A8_UNORM, 1920, 1080, 1, 1, 1, 1, 0, &layout);
	error = view(VK_FORMAT_B8G8R8A8_UNORM, &layout, 1920, 1080, 1, 0, 1, 0, 1, 1, NULL, 0xDA00000000ULL, words);
	expect(error == 0 && words[0] == 0x04371dfc60008864ULL && words[1] == 0x103680000000077fULL, "linear 1920 BGRA8 view: strided, row of 1920 texels");
	lay(VK_FORMAT_R8_UNORM, 100, 10, 1, 1, 1, 1, 0, &layout);
	error = view(VK_FORMAT_R8_UNORM, &layout, 100, 10, 1, 0, 1, 0, 1, 1, NULL, 0x8000003000ULL, words);
	expect(error == 0 && words[0] == 0x0009018c00002d84ULL && words[1] == 0x102000000c00007fULL, "linear R8 view: one channel, row of 128 texels");
	error = view(VK_FORMAT_R8_UNORM, &layout, 100, 10, 1, 0, 1, 0, 1, 1, NULL, 0x8000003002ULL, words);
	expect(error == EINVAL, "an address off four bytes is refused");
	error = view(VK_FORMAT_R8_UNORM, &layout, 100, 10, 1, 0, 1, 0, 1, 1, NULL, 0x10000000000ULL, words);
	expect(error == EINVAL, "an address past forty bits is refused");

	/* The words of samplers (compared once with Mesa's packers). */
	memset(&sampler, 0, sizeof(sampler));
	sampler.max_lod_bits = bits(1000.0f);
	error = powervr_sampler_words(&sampler, words);
	expect(error == 0 && words[0] == 0x00000001df800fffULL && words[1] == 0, "nearest repeat sampler, the level clamp at its largest");
	sampler.mag_filter = VK_FILTER_LINEAR;
	sampler.min_filter = VK_FILTER_NEAREST;
	sampler.address_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	sampler.address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sampler.lod_bias_bits = bits(-1.5f);
	sampler.min_lod_bits = bits(1.5f);
	sampler.max_lod_bits = bits(12.0f);
	sampler.unnormalized = true;
	error = powervr_sampler_words(&sampler, words);
	expect(error == 0 && words[0] == 0x00024211800c0e7fULL, "linear magnification, mirror and border, a negative bias, unnormalized");
	sampler.lod_bias_bits = bits(-20.0f);
	sampler.min_lod_bits = bits(-3.0f);
	sampler.max_lod_bits = 0x7FC00000U;
	error = powervr_sampler_words(&sampler, words);
	expect(error == 0 && (words[0] & 0x1FFFFFFFFULL) == 0, "a bias and clamps below range, and a NaN, read as their least");
	sampler.address_w = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
	error = powervr_sampler_words(&sampler, words);
	expect(error == ENOTSUP, "the mirrored clamp is not taken");

	/* The sizes of descriptors. */
	expect(powervr_descriptor_bytes(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) == 16, "buffer descriptor 16 bytes");
	expect(powervr_descriptor_bytes(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == 16, "uniform descriptor 16 bytes");
	expect(powervr_descriptor_bytes(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 32, "image descriptor 32 bytes");
	expect(powervr_descriptor_bytes(VK_DESCRIPTOR_TYPE_SAMPLER) == 48, "sampler descriptor 48 bytes");
	expect(powervr_descriptor_bytes(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) == 80, "combined descriptor 80 bytes");
	expect(powervr_descriptor_bytes(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) == 0, "a storage image is not taken");

	/*
	 * dc8200.zerocopy=test (p010l): the plane scans a shared frame only
	 * when its row pitch is the loader's (drv_powervr_dc_frame_fits), the
	 * width times 4 bytes, on the 32-byte boundary its import needs
	 * (display.c check_direct_scan).  A linear BGRA8 swapchain image of
	 * the loader's modes has exactly that row pitch; a width whose rows
	 * are not a multiple of 32 bytes is padded and so is copied instead.
	 */
	{
		static const uint32_t modes[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1920, 1080 }, { 3840, 2160 } };

		for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
			expect(lay(VK_FORMAT_B8G8R8A8_UNORM, modes[i][0], modes[i][1], 1, 1, 1, 1, 0, &layout) == 0 &&
			    layout.row_pitch == modes[i][0] * 4U && layout.row_pitch % 32U == 0 &&
			    layout.layer_bytes == (uint64_t)modes[i][0] * 4U * modes[i][1], "a linear BGRA8 image of a loader mode has the loader's row pitch");
		}
		expect(lay(VK_FORMAT_B8G8R8A8_UNORM, 1366, 768, 1, 1, 1, 1, 0, &layout) == 0 && layout.row_pitch == 5472U,
		    "1366 wide: rows padded to 5472 bytes, not the loader's 5464 (copied, not scanned)");
	}

	printf("image-host-test: %d passed, %d failed\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
