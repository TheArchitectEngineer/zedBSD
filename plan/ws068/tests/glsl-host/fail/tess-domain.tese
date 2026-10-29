#version 400
// expect: 0:3: error: a tessellation domain is triangles, quads or isolines
layout(points) in;
void main()
{
}
