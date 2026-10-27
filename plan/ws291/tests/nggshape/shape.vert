/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* nggshape: a vertex's clip-space place, shifted by its instance's offset, and a colour from the place. */
#version 450

layout(location = 0) in vec4 place;
layout(location = 1) in vec2 offset;
layout(location = 0) out vec3 colour;

void main()
{
    colour = vec3(place.xy * 0.5 + 0.5, 0.25);
    gl_Position = place + vec4(offset, 0.0, 0.0);
}
