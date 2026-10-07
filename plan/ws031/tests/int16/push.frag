/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* ws031-p039: a 16-bit integer in push constants, which the compiler refuses. */
#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
layout(push_constant) uniform Push { int16_t v; };
layout(location = 0) out vec4 color;
void main() { color = vec4(float(v)); }
