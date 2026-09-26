#version 300 es
layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
out vec4 v_color;
out vec2 v_uv;
uniform mat4 u_matrix;
uniform vec4 u_tint;

void main()
{
	v_color = a_color * u_tint;
	v_uv = a_uv;
	gl_Position = u_matrix * a_position;
}
