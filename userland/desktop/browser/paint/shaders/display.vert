// zedBSD browser: places one display item's quad (paint/vulkan.c).
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

// The target's size in pixels (x, y); z and w are unused.
layout(push_constant) uniform Frame {
	vec4 size;
} frame;

// The corner of the unit square (xy; zw unused), from the vertex buffer (no
// gl_VertexIndex, which i915's native compiler does not take).
layout(location = 0) in vec4 corner;

// The item, one per instance: its exact rectangle in pixels (left, top,
// right, bottom), its straight color, and the atlas place of a glyph (u, v in
// texels) with its kind (z: 0 a rectangle, 1 a glyph).
layout(location = 1) in vec4 rect;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 atlas;

layout(location = 0) flat out vec4 item_rect;
layout(location = 1) flat out vec4 item_color;
layout(location = 2) flat out vec4 item_atlas;

void main()
{
	// The quad covers every pixel the rectangle touches, so that a partly
	// covered edge pixel is drawn and the fragment shader weighs it.
	vec2 low = floor(rect.xy);
	vec2 high = ceil(rect.zw);
	vec2 position = mix(low, high, corner.xy);

	gl_Position = vec4(position / frame.size.xy * 2.0 - 1.0, 0.0, 1.0);
	item_rect = rect;
	item_color = color;
	item_atlas = atlas;
}
