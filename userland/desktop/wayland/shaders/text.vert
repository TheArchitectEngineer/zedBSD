// zedBSD zdesktop: the glass look's text, many glyphs in one draw (ws075-p031).
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

// Each glyph is two triangles in zdesktop's vertex buffer: the corner's place
// in normalized device coordinates and in the atlas, and the glyph's color and
// opacity (the same at its six corners).
layout(location = 0) in vec2 place;
layout(location = 1) in vec2 atlas;
layout(location = 2) in vec4 color;
layout(location = 3) in float opacity;

layout(location = 0) out vec2 texcoord;
layout(location = 1) flat out vec4 tint;
layout(location = 2) flat out float fade;

void main()
{
	gl_Position = vec4(place, 0.0, 1.0);
	texcoord = atlas;
	tint = color;
	fade = opacity;
}
