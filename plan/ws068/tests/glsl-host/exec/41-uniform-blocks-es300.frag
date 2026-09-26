// expect: 0 255 0 255
// version: 300
#version 300 es
// OpenGL ES 3.00 uniform blocks: a block both stages use (one binding), members read with dynamic indices.
precision highp float;
layout(std140) uniform Shared {
	vec4 base;
	float weights[4];
} shared_block;
flat in int v_index;
out vec4 color;

void main()
{
	int failed = 0;
	if (shared_block.base != vec4(0.0, 1.0, 2.0, 3.0)) failed = 1;
	if (shared_block.weights[v_index] != 12.0) failed = 2;
	if (shared_block.weights[3] != 16.0) failed = 3;
	if (failed == 0)
		color = vec4(0.0, 1.0, 0.0, 1.0);
	else
		color = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
