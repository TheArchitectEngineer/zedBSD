#version 330 core
layout(location = 0) in vec2 a_position;
layout(std140) uniform Lights2 {
	vec4 unused;
	float nine[3];
};
flat out float v_from_vertex;
void main()
{
	v_from_vertex = nine[1] + 1.0;
	gl_Position = vec4(a_position, 0.0, 1.0);
}
