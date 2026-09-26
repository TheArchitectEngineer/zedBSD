#version 330 core
struct Light {
	vec3 position;
	vec4 colour;
};
layout(std140) uniform Scene {
	mat4 view;
	layout(row_major) mat3x4 skew;
	Light lights[4];
	layout(row_major) mat2 turns[2];
};
in vec3 v_position;
out vec4 color;
void main()
{
	color = lights[0].colour * length(v_position) + view[3];
}
