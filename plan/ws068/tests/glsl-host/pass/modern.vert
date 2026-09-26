#version 130
// GLSL 1.30: in and out, unsigned integers, bitwise operators, switch, flat outputs, non-square matrices,
// implicit conversions, array constructors and length().
in vec3 position;
in vec2 uv;
out vec2 v_uv;
flat out int v_index;
noperspective out float v_depth;
uniform mat4x3 u_transform;
uniform uint u_mask;
uniform int u_mode;

const float scales[3] = float[3](1.0, 2.0, 4.0);

void main()
{
	uint bits = u_mask & 0xffu;
	int index = int(bits >> 1u) % 3;
	float scale = 1.0;
	vec3 moved;

	switch (u_mode) {
	case 0:
		scale = scales[index];
		break;
	case 1:
	case 2:
		scale = 2;
		break;
	default:
		scale = float(scales.length());
		break;
	}
	moved = u_transform * vec4(position * scale, 1);
	v_uv = uv;
	v_index = index ^ int(bits & 1u);
	v_depth = moved.z;
	gl_Position = vec4(moved, 1.0);
}
