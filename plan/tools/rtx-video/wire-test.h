/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The video wire round trip's two sides: libvulkan's encoders write the
 * bytes (wire-encode.c), the executor's readers read them (wire-decode.c).
 */

#ifndef PLAN_WS243_TESTS_WIRE_TEST_H
#define PLAN_WS243_TESTS_WIRE_TEST_H

#include <stddef.h>
#include <stdint.h>

/* Encodes a decode with its picture, slices, setup slot and references into out; reports the length, or 0. */
size_t wire_encode_decode(uint8_t *out, size_t capacity);

/* Encodes a begin with three reference slots into out; reports the length, or 0. */
size_t wire_encode_begin(uint8_t *out, size_t capacity);

/* Encodes a parameters create with one SPS and one PPS into out; reports the length, or 0. */
size_t wire_encode_parameters(uint8_t *out, size_t capacity);

#endif
