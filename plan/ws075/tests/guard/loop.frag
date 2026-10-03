// ws075-p021: texture messages in a loop and in a branch inside it (plan/ws075/tests/guard/run.sh): the ENDIF of a
// guarded message inside the loop points at the loop's WHILE.
// Build: glslc --target-env=vulkan1.0 -O loop.frag -o loop.frag.spv
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(set = 0, binding = 0) uniform sampler2D image;
layout(location = 0) in vec2 place;
layout(location = 0) out vec4 color;

void main()
{
	vec4 sum = vec4(0.0);
	int count = int(place.x) % 5;

	for (int i = 0; i < count; i++) {
		if (place.y > float(i) * 8.0)
			sum += texture(image, place * 0.01 + vec2(float(i) * 0.1, 0.0));
	}
	color = sum;
}
