/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The SPIR-V of the executor's own programs (vk-meta.c): the blit that
 * scales or converts, and the vertex and fragment programs of a clear
 * inside a render pass.  The words are in a file of their own, without
 * the kernel, so that a host test compiles the same words.
 */

#ifndef KERN_DRIVERS_GPU_MALI_VK_META_SPIRV_H
#define KERN_DRIVERS_GPU_MALI_VK_META_SPIRV_H

#include <stddef.h>
#include <stdint.h>

/*
 * The push constant bytes of each program, as the programs declare them:
 * the blit's thirteen words, the clear's rectangle and its colour.
 */
#define MALI_META_BLIT_PUSH_BYTES	52U
#define MALI_META_CLEAR_PUSH_BYTES	16U

/* The blit's workgroup: eight by eight texels of one layer. */
#define MALI_META_BLIT_GROUP		8U

/* The blit's flags word: a linear filter, and red and blue exchanged. */
#define MALI_META_BLIT_LINEAR		1U
#define MALI_META_BLIT_SWAP_RB		2U

/* The modules and their counts of words. */
extern const uint32_t mali_meta_blit_spirv[];
extern const size_t mali_meta_blit_words;
extern const uint32_t mali_meta_clear_vertex_spirv[];
extern const size_t mali_meta_clear_vertex_words;
extern const uint32_t mali_meta_clear_color_spirv[];
extern const size_t mali_meta_clear_color_words;

/*
 * Copies the clear's fragment module into words (of count words, at
 * least mali_meta_clear_color_words) with its colour output at a
 * location: 0, or -1 when the module's one Location decoration is not
 * found or words is too small.
 */
int mali_meta_clear_color_at(uint32_t *words, size_t count, uint32_t location);

#endif
