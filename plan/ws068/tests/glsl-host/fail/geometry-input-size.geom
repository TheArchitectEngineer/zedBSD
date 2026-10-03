#version 150
// expect: is not the input primitive's 3 vertices
layout(triangles) in;
layout(points, max_vertices = 1) out;
in vec4 v_colour[2];
void main()
{
	EmitVertex();
}
