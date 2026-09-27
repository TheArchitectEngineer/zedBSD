// expect: 0 255 0 255
// version: 150
#version 150
// GLSL 1.50: a geometry shader between the stages: array inputs, an input block array, gl_in, EmitVertex.
in vec4 g_colour;
flat in int g_primitive;
out vec4 o_colour;
void main()
{
	o_colour = g_colour;
	if (g_primitive < 0 || g_primitive > 1)
		o_colour = vec4(1.0, 0.0, 1.0, 1.0);
}
