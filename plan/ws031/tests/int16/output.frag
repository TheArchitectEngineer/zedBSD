/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* ws031-p039: a 16-bit integer output, which the compiler refuses. */
#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
layout(location = 0) out int16_t v;
void main() { v = 3s; }
