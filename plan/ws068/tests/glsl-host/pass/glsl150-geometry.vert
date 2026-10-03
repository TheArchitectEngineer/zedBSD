#version 150
in vec2 a_position;
out vec3 v_colour;
out Data {
	vec2 uv;
	flat int id;
} data;
void main()
{
	v_colour = vec3(a_position, 1.0);
	data.uv = a_position * 0.5 + 0.5;
	data.id = gl_VertexID;
	gl_Position = vec4(a_position, 0.0, 1.0);
}
