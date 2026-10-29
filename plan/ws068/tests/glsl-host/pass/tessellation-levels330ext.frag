#version 330
// expect: 128 255 255 255
// version: 330
// uniform: u_levels 3.0 6.0
in vec4 t_colour;
uniform vec2 u_levels;
out vec4 o_colour;
void main()
{
	o_colour = t_colour + vec4(u_levels * 0.0, 0.0, 0.0);
}
