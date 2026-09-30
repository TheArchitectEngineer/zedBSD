// zedBSD zdesktop: the glass look's text (ws075-p031): panel.frag's text mode
// (5), the same arithmetic, for many glyphs in one draw.
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) in vec2 texcoord;
layout(location = 1) flat in vec4 tint;
layout(location = 2) flat in float fade;
layout(location = 0) out vec4 result;

void main()
{
	// The glyph's coverage in the atlas, in its color, faded by its opacity.
	float cover = texture(image, texcoord).a;
	vec4 colour = vec4(tint.rgb, 1.0) * tint.a * cover;

	result = colour * fade;
}
