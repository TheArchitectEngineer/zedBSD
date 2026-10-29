#version 330 core
// The direction and cube the lookups of cubearray330ext.frag read.
in vec4 a_position;
out vec4 v_direction;
void main()
{
	v_direction = a_position;
	gl_Position = a_position;
}
