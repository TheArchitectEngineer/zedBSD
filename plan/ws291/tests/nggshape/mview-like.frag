/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* nggshape --mview: the two inputs mview's vertex program gives (texture place, shade) as the colour. */
#version 450

layout(location = 0) in vec2 texture_coordinate;
layout(location = 1) in float shade;
layout(location = 0) out vec4 result;

void main()
{
    result = vec4(texture_coordinate, shade, 1.0);
}
