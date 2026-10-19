/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* The client side of the executor host check (frame-host.c), in plain types only. */

#ifndef WS269_P009_FRAME_HOST_H
#define WS269_P009_FRAME_HOST_H

#include <stddef.h>
#include <stdint.h>

/* The reply region every frame selects, and the bytes of the buffer the compute frame writes. */
#define FRAME_REPLY_BYTES	4096U
#define FRAME_BUFFER_BYTES	4096U

size_t frame_setup(uint32_t reply_blob, uint8_t **bytes);
size_t frame_memory(uint32_t reply_blob, uint64_t memory_bytes, uint8_t **bytes);
size_t frame_compute(uint32_t reply_blob, const uint32_t *spirv, size_t spirv_words, const uint32_t *bad_spirv, size_t bad_words, uint32_t groups, uint8_t **bytes);
void replies_setup(uint8_t *reply);
void replies_memory(uint8_t *reply);
void replies_compute(uint8_t *reply);
void frame_counts(int *passed, int *failed);

#endif
