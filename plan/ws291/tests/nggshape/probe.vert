/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * nggshape's vertex program for --probe: shape.vert that also counts, in
 * a storage buffer, how many times each vertex number ran.
 */
#version 450

layout(location = 0) in vec4 place;
layout(location = 1) in vec2 offset;
layout(location = 0) out vec3 colour;

layout(set = 0, binding = 0, std430) buffer Probe {
    uint runs[];
} probe;

void main()
{
    atomicAdd(probe.runs[gl_VertexIndex], 1u);
    colour = vec3(place.xy * 0.5 + 0.5, 0.25);
    gl_Position = place + vec4(offset, 0.0, 0.0);
}
