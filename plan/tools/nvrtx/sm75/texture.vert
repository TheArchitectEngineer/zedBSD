// ws037-p008: texture reads of a vertex program (no derivatives: the level is zero or explicit).
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(set = 0, binding = 0) uniform sampler2D heights;

layout(location = 0) in vec2 corner;
layout(location = 0) out vec4 value;

void main()
{
	vec4 h = texture(heights, corner);
	vec4 l = textureLod(heights, corner * 0.5, 2.0);
	vec4 o = textureLodOffset(heights, corner, 1.0, ivec2(2, 3));
	vec4 t = texelFetch(heights, ivec2(corner * 8.0), 0);
	gl_Position = vec4(corner, h.x, 1.0);
	value = l + o * 2.0 + t * 4.0;
}
