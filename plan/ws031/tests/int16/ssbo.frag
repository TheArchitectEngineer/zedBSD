/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* ws031-p039: a 16-bit integer stored to a storage buffer, which the compiler refuses. */
#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
layout(binding = 0) buffer Block { int16_t v[]; };
layout(location = 0) out vec4 color;
void main() { v[1] = int16_t(gl_FragCoord.x); color = vec4(1.0); }
