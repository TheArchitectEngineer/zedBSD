#version 400
// expect: 0 255 0 255
// version: 400
// GLSL 4.00: a tessellated quad over the whole target, per-vertex and patch varyings between the stages.
in vec4 t_colour;
out vec4 o_colour;
void main()
{
	o_colour = t_colour;
}
