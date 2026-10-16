// vkload (ws039-p019): a pixel program with a fixed long loop, to keep the graphics engine busy.
// The start and the count come from the interpolated coordinates times zero, so the compiler cannot fold them.
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(location = 0) in vec2 texcoord;
layout(location = 0) out vec4 color;

void main()
{
	uint x = 1u + uint(texcoord.x * 0.0);
	uint left = 16384u + uint(texcoord.y * 0.0);

	while (left != 0u) {
		x = x * 1664525u + 1013904223u;
		left--;
	}
	color = vec4(float(x & 255u) / 255.0, float((x >> 8) & 255u) / 255.0, 0.0, 1.0);
}
