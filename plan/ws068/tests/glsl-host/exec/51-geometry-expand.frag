// expect: 0 0 255 255
// version: 150
#version 150
// GLSL 1.50: a geometry shader emitting vertices of its own, by gl_PrimitiveIDIn.
in vec4 g_colour;
out vec4 o_colour;
void main()
{
	o_colour = g_colour;
}
