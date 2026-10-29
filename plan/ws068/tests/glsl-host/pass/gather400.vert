#version 400
// The coordinate the gathers of gather400.frag read.
in vec4 a_position;
out vec3 v_coordinate;
void main()
{
	v_coordinate = a_position.xyz;
	gl_Position = a_position;
}
