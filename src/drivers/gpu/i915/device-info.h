/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GT and engine information of the device.
 *
 * Before any engine runs, the driver reads what the fuses and the clock
 * registers say about this particular part: how fast the command streamer
 * timestamp ticks, which subslices and EUs survived fusing, which L3 banks
 * replicated registers can be steered to, and which engines exist.  This is
 * the Linux intel_gt_init_mmio() step.
 *
 * Easy to get wrong, and checked by the tests:
 *
 *   - GEN11_EU_DISABLE is a disable mask with one bit per pair of EUs; each
 *     set bit of its inverse enables two EUs.
 *   - Gen12 counts Dual-Subslices: at most 1 slice, 6 DSS, 16 EUs per DSS.
 *   - GEN11_GT_VEBOX_VDBOX_DISABLE is a disable register on media versions
 *     before 12.50, so it is inverted before use.
 */

#ifndef DRIVERS_GPU_I915_DEVICE_INFO_H
#define DRIVERS_GPU_I915_DEVICE_INFO_H

#include <stdint.h>

struct i915_mmio;

/* How many engines one device can have: RCS0, BCS0, VCS0, VCS2, VECS0, and one spare. */
#define I915_MAX_ENGINES		6

/* How many subslices the SSEU masks describe. */
#define I915_SSEU_MAX_SUBSLICES		6

/*
 * The engine identifiers (Linux enum intel_engine_id).
 *
 * The values are Linux's own, because the interrupt code looks an engine up
 * by class and instance through them.
 */
#define I915_RCS0			0
#define I915_BCS0			1
#define I915_VCS0			8
#define I915_VCS2			10
#define I915_VECS0			16

/* The engine classes (Linux enum intel_engine_class, the GuC and interrupt ordinals). */
#define I915_RENDER_CLASS		0
#define I915_VIDEO_DECODE_CLASS		1
#define I915_VIDEO_ENHANCEMENT_CLASS	2
#define I915_COPY_ENGINE_CLASS		3
#define I915_OTHER_CLASS		4
#define I915_MAX_ENGINE_CLASS		4
#define I915_MAX_ENGINE_INSTANCE	7

/*
 * The slice, subslice and EU layout that survived fusing.
 *
 * It is the subset of Linux's struct sseu_dev_info this driver uses.  It is
 * filled once by the GT information step and only read afterwards; the
 * context images and the workarounds are built from it.
 */
struct i915_sseu {
	/* The layout the hardware generation allows at most. */
	uint8_t max_slices;
	uint8_t max_subslices;
	uint8_t max_eus_per_subslice;

	/* The slices that are present. */
	uint8_t slice_mask;

	/* The (dual-)subslices of slice 0 that are present. */
	uint16_t subslice_mask;

	/* The EUs present in each subslice of slice 0. */
	uint16_t eu_mask[I915_SSEU_MAX_SUBSLICES];

	/* How many EUs one subslice has, and how many the whole GT has. */
	uint16_t eu_per_subslice;
	uint16_t eu_total;

	/* Nonzero when the hardware can power-gate a whole slice. */
	int has_slice_pg;

	/* Nonzero once the fuses have been decoded. */
	int valid;
};

/*
 * One engine the fuses left enabled.
 *
 * It is the subset of Linux's struct intel_engine_cs that is known before
 * any submission exists.  It lives inside the GT information for the life of
 * the device; the execution side points at it rather than copying it.
 */
struct i915_engine_info {
	/* The engine identifier, one of I915_RCS0 and the rest. */
	int id;

	/* The engine class and the instance within that class. */
	int class;
	int instance;

	/* Where the engine's register block starts. */
	uint32_t mmio_base;

	/* The engine's bit in the engine mask. */
	uint32_t mask;

	/* The GDRST bit that resets this engine alone. */
	uint32_t reset_domain;

	/* How large the engine's logical ring context image is. */
	uint32_t context_size;

	/* The instance number user space sees for this engine. */
	unsigned logical_instance;

	/* The engine's name, a static string. */
	const char *name;

	/* The optional features user space may use on this engine. */
	uint32_t uabi_capabilities;

	/* Nonzero for a slot that holds a built engine. */
	int in_use;
};

/*
 * The GT information of one device.
 *
 * It is filled by drv_i915_gt_init_mmio() during the device start and only
 * read afterwards.  The owner keeps it for the life of the device.
 */
struct i915_gt_info {
	/* The command streamer timestamp frequency in Hz, and its period. */
	uint32_t clock_frequency;
	uint32_t clock_period_ns;

	/* Nonzero when the timestamp frequency could be determined. */
	int clock_valid;

	/* The slices, subslices and EUs that survived fusing. */
	struct i915_sseu sseu;

	/* The L3 banks that replicated registers may be steered to. */
	uint32_t l3bank_mask;

	/* Nonzero once the L3 bank steering table applies. */
	int l3bank_steering;

	/* The engines that survived fusing, in table order. */
	struct i915_engine_info engines[I915_MAX_ENGINES];
	unsigned num_engines;

	/* The engine mask after the media fuses were applied. */
	uint32_t engine_mask;

	/* The video decode instances that can reach a scaler and format converter. */
	uint32_t vdbox_sfc_access;

	/* The instances whose scaler and format converter are present. */
	uint32_t sfc_mask;

	/* Nonzero when a GPU fault was pending at start, and the fault register seen. */
	int fault_valid_seen;
	uint32_t fault_reg;

	/* Nonzero once the information has been filled. */
	int inited;
};

int drv_i915_gt_init_mmio(struct i915_gt_info *gt, int graphics_ver, uint32_t platform_engine_mask, struct i915_mmio *mmio);

#endif
