/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The SPIR-V the PowerVR driver runs before it has a compiler: a compute
 * shader that writes one storage buffer word per invocation.
 *
 * The shader's one store writes, at index x + index_addend of a runtime
 * array of 32-bit words in one storage buffer, the value
 * x * multiplier + addend, where x is gl_GlobalInvocationID.x; the
 * workgroup is local_x invocations in x only.  The driver builds that
 * store for the core itself (compute-program.c); every other shader is
 * refused.  The same code runs on the build host.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_SPIRV_FILL_H
#define KERN_DRIVERS_GPU_POWERVR_SPIRV_FILL_H

#include <stddef.h>
#include <stdint.h>

/*
 * What a shader of the subset does: its workgroup's size, the set and
 * binding of the buffer it writes, the index it writes at past x, and
 * the multiplier and addend of its value.
 */
struct powervr_spirv_fill {
	uint32_t local_x;
	uint32_t set;
	uint32_t binding;
	uint32_t index_addend;
	uint32_t multiplier;
	uint32_t addend;
};

/* The highest id bound a shader of the subset may declare. */
#define POWERVR_SPIRV_FILL_IDS		256U

/*
 * What the parse knows of one id: what it stands for, the value a*x + c
 * it holds (x being gl_GlobalInvocationID.x), and its decorations.
 * The caller gives the parse a table of POWERVR_SPIRV_FILL_IDS of them.
 */
struct powervr_spirv_value {
	uint8_t kind;
	uint8_t flags;
	uint32_t a;
	uint32_t c;
	uint32_t set;
	uint32_t binding;
};

int powervr_spirv_fill_parse(const uint32_t *words, size_t count, struct powervr_spirv_value *values, struct powervr_spirv_fill *fill);

#endif /* KERN_DRIVERS_GPU_POWERVR_SPIRV_FILL_H */
