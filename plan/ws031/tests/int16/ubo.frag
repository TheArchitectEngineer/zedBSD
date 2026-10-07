/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* ws031-p039: a 16-bit integer in a uniform block (offset 0), which the compiler refuses. */
#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
layout(binding = 0) uniform Block { int16_t v; int w; };
layout(location = 0) out vec4 color;
void main() { color = vec4(float(v), float(w), 0.0, 1.0); }
