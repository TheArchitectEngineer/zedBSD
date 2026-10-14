// ws037-p008: the 2D texture operations the nvrtx compiler takes beyond a plain sample: constant texel offsets,
// an explicit level and a bias (with and without an offset), texel fetches (with an offset, which the frontend adds
// to the coordinate) and an integer texture's fetch (the general message).
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(set = 0, binding = 0) uniform sampler2D image;
layout(set = 0, binding = 1) uniform usampler2D labels;

layout(location = 0) in vec2 coordinate;
layout(location = 1) flat in ivec2 texel;
layout(location = 2) in float level;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 other;

void main()
{
	vec4 a = textureOffset(image, coordinate, ivec2(1, -2));
	vec4 b = textureLod(image, coordinate, level);
	vec4 c = textureLodOffset(image, coordinate, level, ivec2(-8, 7));
	vec4 d = texture(image, coordinate, level);
	vec4 e = texelFetch(image, texel, 1);
	vec4 f = texelFetchOffset(image, texel, 0, ivec2(3, 1));
	uvec4 g = texelFetch(labels, texel, 0);
	color = a + b * 2.0 + c * 4.0;
	other = d + e * 8.0 + f * 16.0 + vec4(g) * 32.0;
}
