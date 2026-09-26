// expect: 0 255 0 255
// version: 330
#version 330 core
// GLSL 3.30: an in block from the vertex shader, located outputs, gl_InstanceID.
in VS_OUT {
	vec3 value;
	flat int index;
	flat uint bits;
} fs_in;
in vec2 v_plain;
layout(location = 0) out vec4 color;

void main()
{
	int failed = 0;
	if (any(greaterThan(abs(fs_in.value - vec3(0.25, 0.5, 0.75)), vec3(0.001)))) failed = 1;
	if (fs_in.index != 7) failed = 2;
	if (fs_in.bits != 0x80000000u) failed = 3;
	if (abs(v_plain.y - 3.0) > 0.001) failed = 4;
	if (failed == 0)
		color = vec4(0.0, 1.0, 0.0, 1.0);
	else
		color = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
