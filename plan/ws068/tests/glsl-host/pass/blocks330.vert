#version 330 core
// Uniform blocks with and without instance names, row-major matrices, nested structs, used in both stages.
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
uniform Object {
	mat4 model;
	bool visible;
} object;
layout(location = 0) in vec3 position;
out vec3 v_position;
void main()
{
	vec4 world = object.model * vec4(position, 1.0);
	v_position = world.xyz + lights[1].position + vec3(turns[1][0], 0.0) + (skew * vec3(1.0)).xyz;
	gl_Position = object.visible ? view * world : vec4(0.0);
}
