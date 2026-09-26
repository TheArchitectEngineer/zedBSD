#version 300 es
in vec2 a_position;
layout(std140) uniform Shared {
	vec4 base;
	float weights[4];
} shared_block;
flat out int v_index;
void main()
{
	v_index = int(shared_block.base.y) + 1;
	gl_Position = vec4(a_position, 0.0, 1.0);
}
