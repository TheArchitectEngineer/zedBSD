/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* FIFO native preparation owns every uploaded input independently of public Vulkan identities. */
#ifndef KERN_DRIVERS_GPU_BCM2711_VULKAN_NATIVE_DRAW_H
#define KERN_DRIVERS_GPU_BCM2711_VULKAN_NATIVE_DRAW_H

#include "drivers/gpu/bcm2711/native-storage.h"
#include "drivers/gpu/bcm2711/native-bin.h"
#include "drivers/gpu/bcm2711/vulkan-uniform.h"

/* Each canonical slot needs at most one image and one descriptor allocation, plus three code/uniform pairs and fetch/default/shader storage. */
#define BCM2711_VULKAN_DRAW_STORAGE (BCM2711_VULKAN_PIPELINE_SETS * BCM2711_VULKAN_LAYOUT_BINDINGS * 2U + 9U)

/* One converted raster belongs to the draw that allocated it; a pass may borrow it until whole-pass retirement. */
struct bcm2711_vulkan_native_texture_copy {
	const struct bcm2711_vulkan_resource *image;
	struct bcm2711_native_storage *pixels;
};

/* One complete draw owns its uploads and borrows earlier pass textures; the enclosing pass retains all draws through uncertainty. */
struct bcm2711_vulkan_native_draw {
	/* The enclosing pass links fully owned draw roots; individual release never traverses this link. */
	struct bcm2711_vulkan_native_draw *next;
	struct bcm2711_v3d_space *space;
	struct bcm2711_native_storage *storage[BCM2711_VULKAN_DRAW_STORAGE];
	struct bcm2711_vulkan_native_binding bindings[BCM2711_VULKAN_PIPELINE_SETS][BCM2711_VULKAN_LAYOUT_BINDINGS];
	/* Earlier draws belong to the same unsubmitted pass; this construction-only link is cleared before publication. */
	const struct bcm2711_vulkan_native_draw *earlier;
	struct bcm2711_vulkan_native_texture_copy textures[BCM2711_VULKAN_PIPELINE_SETS * BCM2711_VULKAN_LAYOUT_BINDINGS];
	uint32_t texture_count;
	/* Preparation diagnostics include scheduler delay and never affect admission. */
	uint64_t texture_ticks;
	uint64_t copy_ticks;
	uint64_t program_ticks;
	uint64_t record_ticks;
	uint64_t bytes;
	uint32_t count;
	uint32_t shader;
	uint32_t attributes;
	uint32_t vertices;
	/* Numerical BCL state refers only to this draw or retained earlier draws in the same pass. */
	uint32_t bin_bytes;
	uint8_t bin[BCM2711_NATIVE_BIN_BYTES];
};

/* One unpublished internal texture quad borrows an actual retained source and owns copied geometry/programs until native upload finishes. */
struct bcm2711_vulkan_meta_draw {
	struct bcm2711_vulkan_pipeline pipeline;
	struct bcm2711_shader_key key;
	struct bcm2711_vulkan_resource *image;
	uint32_t vertices[36];
	uint32_t viewport[6];
	uint32_t width;
	uint32_t height;
	VkRect2D area;
	VkFilter filter;
	/* Copy preserves raw channel bytes; blit instead uses the source's canonical colour interpretation. */
	bool raw;
};

int bcm2711_vulkan_native_meta_draw_create(struct bcm2711_v3d_space *space, const struct bcm2711_vulkan_meta_draw *meta, uint64_t *available, struct bcm2711_vulkan_native_draw **draw);

/*
 * Controller serialization and prior queue visibility are required; available is the remaining padded job budget.
 * Earlier draws must belong to this same pass and remain alive until the entire pass retires; NULL gives an independent draw.
 */
int bcm2711_vulkan_native_draw_create(struct bcm2711_v3d_space *space, const struct bcm2711_vulkan_prepared_event *event, uint64_t *available, const struct bcm2711_vulkan_native_draw *earlier, struct bcm2711_vulkan_native_draw **draw);

/* true consumes/nulls all storage even when translation teardown fails; false keeps the whole root unchanged. */
int bcm2711_vulkan_native_draw_release(struct bcm2711_vulkan_native_draw **draw, bool retired);

/* Internal lowering runs only while the unpublished draw root and prepared event are protected by the controller mutex. */
int bcm2711_vulkan_native_bin_prepare(const struct bcm2711_vulkan_prepared_event *event, struct bcm2711_vulkan_native_draw *draw);

#endif
