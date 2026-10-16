// vkhang (ws039-p013): a pixel program that ends: the 3x + 1 walk from 27, 111 steps to 1 (the control).
// The start comes from the interpolated coordinate times zero, so the compiler cannot see it is constant.
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(location = 0) in vec2 texcoord;
layout(location = 0) out vec4 color;

void main()
{
	uint x = 27u + uint(texcoord.x * 0.0);
	uint steps = 0u;

	while (x != 1u) {
		x = (x & 1u) != 0u ? x * 3u + 1u : x / 2u;
		steps++;
	}
	color = vec4(float(steps & 255u) / 255.0, 0.0, 0.0, 1.0);
}
