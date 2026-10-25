/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The formats of a GHCI device: the bytes of a texel of each VkFormat the
 * driver lays out, and the format features the device reports.
 *
 * The GPU says which formats its fixed-function units take through the
 * FMT extended capability (GHCI section 4, draft): the driver reads every
 * VkFormat's features once at the start and adds the features its shader
 * programs provide (storage images and storage texel buffers, which the
 * compiler reaches through memory).  A GPU without the capability takes
 * the formats every GHCI GPU has: RGBA8 and BGRA8 colour, and the D24S8
 * and D32 depth formats.
 *
 * Values referenced: the VkFormat values and the texel sizes of the
 * Vulkan 1.0 core formats (vk.xml, the formats table of the Vulkan
 * specification).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <libc/vulkan/vulkan_core.h>

#include "vk.h"
#include "vk-engine.h"

/* The FMT extended capability and its registers (GHCI section 4, draft). */
#define FMT_CAPABILITY			10U
#define FMT_INDEX			0x0U
#define FMT_OPTIMAL			0x4U
#define FMT_LINEAR			0x8U
#define FMT_BUFFER			0xcU
#define FMT_LIMITS			0x10U

/* The colour attachments FMT_LIMITS reports ([3:0]). */
#define FMT_LIMITS_ATTACHMENTS(w)	((w) & 0xfU)

/* The features of a sampled colour format the units filter, render to and blend. */
#define COLOR_FEATURES			(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
					 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | \
					 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | \
					 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | \
					 VK_FORMAT_FEATURE_BLIT_SRC_BIT | \
					 VK_FORMAT_FEATURE_BLIT_DST_BIT | \
					 VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | \
					 VK_FORMAT_FEATURE_TRANSFER_DST_BIT)

/* The features of a depth format the units render to and sample. */
#define DEPTH_FEATURES			(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
					 VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | \
					 VK_FORMAT_FEATURE_BLIT_SRC_BIT | \
					 VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | \
					 VK_FORMAT_FEATURE_TRANSFER_DST_BIT)

/*
 * One run of consecutive VkFormat values whose texels have the same
 * bytes.  The table is constant and lives as long as the kernel.
 */
struct format_run {
	uint32_t first;
	uint32_t last;
	uint32_t bytes;
};

/*
 * The formats the driver lays out: the Vulkan 1.0 core formats a GHCI GPU
 * may take (the compressed ones are not among them yet).
 */
static const struct format_run format_runs[] = {
	{ VK_FORMAT_B4G4R4A4_UNORM_PACK16, VK_FORMAT_R5G6B5_UNORM_PACK16, 2U },
	{ VK_FORMAT_A1R5G5B5_UNORM_PACK16, VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2U },
	{ VK_FORMAT_R8_UNORM, VK_FORMAT_R8_SRGB, 1U },
	{ VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_SRGB, 2U },
	{ VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A8B8G8R8_SRGB_PACK32, 4U },
	{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2B10G10R10_SINT_PACK32, 4U },
	{ VK_FORMAT_R16_UNORM, VK_FORMAT_R16_SFLOAT, 2U },
	{ VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16_SFLOAT, 4U },
	{ VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, 8U },
	{ VK_FORMAT_R32_UINT, VK_FORMAT_R32_SFLOAT, 4U },
	{ VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32_SFLOAT, 8U },
	{ VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32_SFLOAT, 12U },
	{ VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_SFLOAT, 16U },
	{ VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 4U },
	{ VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM, 2U },
	{ VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT, 4U },
	{ VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, 4U }
};

/*
 * The formats a compiled program reads and writes through memory as
 * storage images and storage texel buffers (the storage column of the
 * Vulkan 1.0 required formats).
 */
static const uint32_t storage_formats[] = {
	VK_FORMAT_R8G8B8A8_UNORM,
	VK_FORMAT_R8G8B8A8_SNORM,
	VK_FORMAT_R8G8B8A8_UINT,
	VK_FORMAT_R8G8B8A8_SINT,
	VK_FORMAT_R16G16B16A16_UINT,
	VK_FORMAT_R16G16B16A16_SINT,
	VK_FORMAT_R16G16B16A16_SFLOAT,
	VK_FORMAT_R32_UINT,
	VK_FORMAT_R32_SINT,
	VK_FORMAT_R32_SFLOAT,
	VK_FORMAT_R32G32_UINT,
	VK_FORMAT_R32G32_SINT,
	VK_FORMAT_R32G32_SFLOAT,
	VK_FORMAT_R32G32B32A32_UINT,
	VK_FORMAT_R32G32B32A32_SINT,
	VK_FORMAT_R32G32B32A32_SFLOAT
};

static int storage_format(uint32_t format);
static void fallback_features(uint32_t format, uint32_t *optimal, uint32_t *linear, uint32_t *buffer);

/*
 * Reports the bytes of a texel of a VkFormat.
 */
uint32_t
ghci_vk_format_bytes(
	uint32_t format,
	uint32_t *block)
{
	unsigned index;

	/* No format of the table is block compressed. */
	*block = 0U;

	/* The run that holds the format. */
	for (index = 0U; index < sizeof(format_runs) / sizeof(format_runs[0]); index++) {
		if (format < format_runs[index].first)
			continue;
		if (format > format_runs[index].last)
			continue;

		/* Succeeded: the run's texel bytes. */
		return format_runs[index].bytes;
	}

	/* A format the driver does not lay out. */
	return 0U;
}

/*
 * Reads every format's features from the GPU's FMT capability, once at
 * the start, into the engine's table.
 */
void
ghci_vk_format_load(
	struct ghci_device *device)
{
	struct ghci_vk_engine *engine;
	uint32_t registers;
	uint32_t version;
	uint32_t format;
	uint32_t limits;
	int error;

	/* A GPU without the capability keeps the fallback table. */
	engine = device->vk;
	engine->fmt_present = 0;
	engine->color_attachments = 1U;
	error = ghci_extcap_find(ghci_read_context, device, device->extcap, device->register_bytes, FMT_CAPABILITY, &registers, &version);
	if (error != 0)
		return;

	/* Each VkFormat's three feature words, by its index. */
	for (format = 0U; format < GHCI_VK_FORMATS; format++) {
		ghci_write(device, registers + FMT_INDEX, format);
		engine->fmt_optimal[format] = ghci_read(device, registers + FMT_OPTIMAL);
		engine->fmt_linear[format] = ghci_read(device, registers + FMT_LINEAR);
		engine->fmt_buffer[format] = ghci_read(device, registers + FMT_BUFFER);
	}

	/* The colour attachments the render output takes. */
	limits = ghci_read(device, registers + FMT_LIMITS);
	engine->color_attachments = FMT_LIMITS_ATTACHMENTS(limits);
	if (engine->color_attachments == 0U)
		engine->color_attachments = 1U;

	/* The table is the GPU's from now on. */
	engine->fmt_present = 1;
}

/*
 * Reports the features of a format for optimal and linear tiling and for buffers.
 */
void
ghci_vk_format_features(
	struct ghci_device *device,
	uint32_t format,
	uint32_t *optimal,
	uint32_t *linear,
	uint32_t *buffer)
{
	struct ghci_vk_engine *engine;
	uint32_t bytes;
	uint32_t block;
	int storage;

	/* Nothing for a format the driver does not lay out. */
	*optimal = 0U;
	*linear = 0U;
	*buffer = 0U;
	bytes = ghci_vk_format_bytes(format, &block);
	if (bytes == 0U)
		return;
	if (format >= GHCI_VK_FORMATS)
		return;

	/* The GPU's own table, or the formats every GHCI GPU has. */
	engine = device->vk;
	if (engine->fmt_present) {
		*optimal = engine->fmt_optimal[format];
		*linear = engine->fmt_linear[format];
		*buffer = engine->fmt_buffer[format];
	} else {
		fallback_features(format, optimal, linear, buffer);
	}

	/* The storage images and storage texel buffers the compiled programs reach through memory. */
	storage = storage_format(format);
	if (storage) {
		*optimal |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		*linear |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		*buffer |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
	}
}

/* Reports whether programs reach a format through memory as a storage image: 1 when they do. */
static int
storage_format(
	uint32_t format)
{
	unsigned index;

	/* Looks the format up in the storage column. */
	for (index = 0U; index < sizeof(storage_formats) / sizeof(storage_formats[0]); index++) {
		if (storage_formats[index] == format)
			return 1;
	}

	/* Not a storage format. */
	return 0;
}

/* Fills the features of the formats every GHCI GPU has, for a GPU without the FMT capability. */
static void
fallback_features(
	uint32_t format,
	uint32_t *optimal,
	uint32_t *linear,
	uint32_t *buffer)
{
	/* RGBA8 and BGRA8 colour, linear and optimal alike: every image is laid out in linear rows. */
	if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM) {
		*optimal = COLOR_FEATURES;
		*linear = COLOR_FEATURES;
		*buffer = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
		return;
	}

	/* The depth formats of the render output. */
	if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT) {
		*optimal = DEPTH_FEATURES;
		return;
	}

	/* The 32-bit float vertex formats of the vertex fetcher. */
	if (format == VK_FORMAT_R32_SFLOAT ||
	    format == VK_FORMAT_R32G32_SFLOAT ||
	    format == VK_FORMAT_R32G32B32_SFLOAT ||
	    format == VK_FORMAT_R32G32B32A32_SFLOAT)
		*buffer = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
}
