#version 150 compatibility
// GLSL 1.50 in the compatibility profile keeps attribute, varying and gl_FragColor.
in vec4 a_position;
out vec2 v_uv;
out Data {
	vec4 colour;
} data;
void main()
{
	v_uv = a_position.xy;
	data.colour = vec4(1.0);
	gl_Position = a_position;
}
