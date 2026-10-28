#version 450
// ws269 p014: the vertex and instance index, as gpu-selftest's draw and
// depth steps may place a triangle without a vertex buffer.
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

void main()
{
	float x = float(gl_VertexIndex & 3) * 0.5 - 0.5;
	float y = float(gl_VertexIndex >> 2) * 0.25;

	gl_Position = vec4(x + float(gl_InstanceIndex) * 0.125, y, 0.5, 1.0);
}
