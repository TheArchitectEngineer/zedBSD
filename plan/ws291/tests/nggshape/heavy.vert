/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * nggshape --heavy: the same place and colour as shape.vert, through many
 * values live at once (as many VGPRs as mview's vertex program takes).
 */
#version 450

layout(location = 0) in vec4 place;
layout(location = 1) in vec2 offset;
layout(location = 0) out vec3 colour;

void main()
{
    vec4 a = place * 1.01;
    vec4 b = place.yzwx * 0.99;
    vec4 c = place.zwxy + a;
    vec4 d = place.wxyz - b;
    vec4 e = a * b + c;
    vec4 f = c * d - a;
    vec4 g = e * f + b;
    vec4 h = sqrt(abs(g)) + d;
    float m = dot(a, b) + dot(c, d) + dot(e, f) + dot(g, h);
    colour = vec3(place.xy * 0.5 + 0.5, 0.25) + vec3(m * 0.0);
    gl_Position = place + vec4(offset, 0.0, 0.0) + vec4(m * 0.0);
}
