#version 150
in vec3 g_colour;
flat in int g_id;
out vec4 o_colour;
void main()
{
	o_colour = vec4(g_colour, float(g_id) + float(gl_PrimitiveID));
}
