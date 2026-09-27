#version 150
// expect: inputs are arrays of the vertices
layout(points) in;
layout(points, max_vertices = 1) out;
in vec4 v_colour;
void main()
{
	EmitVertex();
}
