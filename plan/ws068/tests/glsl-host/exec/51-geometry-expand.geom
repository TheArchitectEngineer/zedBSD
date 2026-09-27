#version 150
// The first triangle becomes a quad over the whole target; the second emits nothing.
layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;
out vec4 g_colour;
void main()
{
	if (gl_PrimitiveIDIn != 0)
		return;
	g_colour = vec4(0.0, 0.0, 1.0, 1.0);
	gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
	EmitVertex();
	gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
	EmitVertex();
	gl_Position = vec4(-1.0, 1.0, 0.0, 1.0);
	EmitVertex();
	gl_Position = vec4(1.0, 1.0, 0.0, 1.0);
	EmitVertex();
	EndPrimitive();
}
