#version 150
in vec2 a_position;
out float v_value;
out Data {
	vec2 corner;
} data;
void main()
{
	v_value = 0.25;
	data.corner = a_position;
	gl_Position = vec4(a_position, 0.0, 1.0);
}
