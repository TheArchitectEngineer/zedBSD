#version 330 core
layout(location = 0) in vec2 a_position;
out VS_OUT {
	vec3 value;
	flat int index;
	flat uint bits;
} vs_out;
out vec2 v_plain;
void main()
{
	vs_out.value = vec3(0.25, 0.5, 0.75);
	vs_out.index = 7 + gl_InstanceID;
	vs_out.bits = 1u << 31;
	v_plain = vec2(1.0, 3.0);
	gl_Position = vec4(a_position, 0.0, 1.0);
}
