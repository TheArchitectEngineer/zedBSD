// ws075-p009: OpSwitch in the i915 compiler (plan/ws031/tests/i915-vk-lower-test.c, test_switch).
// The case index k = int(x) % 7 of the pixel column: two literals of one target (1, 2), a fall-through (3 into 4),
// a case that shares the default's target (5), the default (6); and an early return for the lower half, which
// glslc -O's merge-return pass wraps in a default-only OpSwitch (the browser's display.frag has that shape).
// Build: glslc --target-env=vulkan1.0 [-O] switch.frag -o switch[-O].frag.spv
// Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#version 450

layout(location = 0) in vec4 place;
layout(location = 0) out vec4 color;

void main()
{
	int k = int(place.x) % 7;
	float r = 0.0;

	switch (k) {
	case 0:
		r = 0.125;
		break;
	case 1:
	case 2:
		r = 0.25;
		break;
	case 3:
		r = 0.5;
	case 4:
		r = r + 0.0625;
		break;
	case 5:
	default:
		r = 0.875;
		break;
	}

	if (place.y > 32.0) {
		color = vec4(r, 0.5, 0.0, 1.0);
		return;
	}

	color = vec4(r, 0.25, float(k) * 0.125, 1.0);
}
