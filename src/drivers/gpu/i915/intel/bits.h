/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The bit helpers the transcribed Intel definitions of this directory are
 * written with.
 *
 * The Linux register headers build their fields with REG_BIT, BIT, BIT_ULL,
 * REG_GENMASK, GENMASK, GENMASK_ULL and PAGE_SIZE.  The transcriptions here
 * were rewritten to use the helpers below instead (the "Rewrites" line of each
 * header names the mapping).  The helpers are this driver's own definitions,
 * not copies of the Linux ones.
 */

#ifndef DRIVERS_GPU_I915_INTEL_BITS_H
#define DRIVERS_GPU_I915_INTEL_BITS_H

/* Bit helpers replacing the Linux register helper macros. */
#define I915_INC_BIT(n)			(1U << (n))
#define I915_INC_BIT64(n)		(1ULL << (n))
#define I915_INC_GENMASK(h, l)		(((0xffffffffU) >> (31U - (h))) & ((0xffffffffU) << (l)))
#define I915_INC_GENMASK64(h, l)	(((0xffffffffffffffffULL) >> (63U - (h))) & ((0xffffffffffffffffULL) << (l)))
#define I915_INC_PAGE_SIZE		4096U

#endif /* DRIVERS_GPU_I915_INTEL_BITS_H */
