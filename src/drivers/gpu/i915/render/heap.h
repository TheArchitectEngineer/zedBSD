/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The layout of the render paths' state object, shared by state.c, draw.c
 * and blit.c.
 *
 * One session object holds every heap a draw or a rectangle points at, at
 * fixed offsets from its start.  The surface, dynamic and instruction heaps
 * are the bases STATE_BASE_ADDRESS programs; the offsets inside them are the
 * pointers the state packets carry.  A draw and a rectangle both rewrite the
 * same object, so they must agree on every offset here.
 *
 *   0x0000  surface state heap: the binding table, then two surface states
 *   0x1000  dynamic state heap: colour calc, blend, viewports, scissor,
 *           sampler and coarse pixel state
 *   0x2000  push constant buffer (absolute address in 3DSTATE_CONSTANT_VS)
 *   0x2800  the three vertices of a rectangle
 *   0x3000  scratch for the post-sync writes
 *   0x4000  instruction heap: the vertex kernel at 0, the pixel kernel at 4 KiB
 */

#ifndef DRIVERS_GPU_I915_RENDER_HEAP_H
#define DRIVERS_GPU_I915_RENDER_HEAP_H

/* The size of the state object. */
#define I915_GFX_STATE_BYTES		65536U

/* The surface state base, and the offsets from it. */
#define I915_GFX_SURFACE_HEAP		0x0000U
#define I915_GFX_BINDING_TABLE		0x0000U
#define I915_GFX_RSS_TARGET		0x0040U
#define I915_GFX_RSS_TEXTURE		0x0080U

/* The dynamic state base, and the offsets from it. */
#define I915_GFX_DYNAMIC_HEAP		0x1000U
#define I915_GFX_DYN_COLOR_CALC		0x0000U
#define I915_GFX_DYN_BLEND		0x0040U
#define I915_GFX_DYN_CC_VIEWPORT	0x0080U
#define I915_GFX_DYN_SF_CLIP_VIEWPORT	0x00c0U
#define I915_GFX_DYN_SCISSOR		0x0100U
#define I915_GFX_DYN_SAMPLER		0x0140U
#define I915_GFX_DYN_CPS		0x0180U

/* The push constant buffer, addressed absolutely by 3DSTATE_CONSTANT_VS. */
#define I915_GFX_PUSH_BUFFER		0x2000U

/* The three vertices of a rectangle, twelve floats each. */
#define I915_GFX_RECT_VERTICES		0x2800U
#define I915_GFX_RECT_VERTEX_BYTES	48U

/* Where the post-sync writes of the depth workaround land. */
#define I915_GFX_SCRATCH		0x3000U

/* The instruction base, the kernel offsets from it and the heap's size. */
#define I915_GFX_INSTRUCTION_HEAP	0x4000U
#define I915_GFX_VS_KERNEL		0x0000U
#define I915_GFX_PS_KERNEL		0x1000U
#define I915_GFX_INSTRUCTION_BYTES	0x4000U

/* The size of the batch object a draw or a rectangle is written into. */
#define I915_GFX_BATCH_BYTES		16384U

/*
 * The largest vertex shader thread count of the target (intel_device_info
 * max_vs_threads of Alder Lake-P GT2).
 *
 * XXX: a property of the device, not of the render path; it belongs to the
 * device information and is fixed here for the one target.
 */
#define I915_GFX_MAX_VS_THREADS		546U

/*
 * The MOCS table entry the render paths use for their surfaces, vertex
 * buffers and state: index 3, uncached (see GEN12_MOCS()).
 */
#define I915_MOCS_UNCACHED_INDEX	3U

#endif
