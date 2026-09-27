#version 150
// expect: an output layout names points, line_strip or triangle_strip
layout(triangles) in;
layout(triangles, max_vertices = 3) out;
void main()
{
	EmitVertex();
}
