/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The WS243 wire round trip's two sides: libvulkan's encoders write the
 * bytes (wire-encode.c), the executor's readers read them (wire-decode.c).
 */

#ifndef PLAN_WS243_TESTS_WIRE_TEST_H
#define PLAN_WS243_TESTS_WIRE_TEST_H

#include <stddef.h>
#include <stdint.h>

size_t wire_encode_decode(uint8_t *out, size_t capacity);
size_t wire_encode_begin(uint8_t *out, size_t capacity);
size_t wire_encode_parameters(uint8_t *out, size_t capacity);

#endif
