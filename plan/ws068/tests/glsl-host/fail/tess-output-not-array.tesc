#version 400
// expect: 0:4: error: a tessellation shader's per-vertex inputs and outputs are arrays
layout(vertices = 3) out;
out vec4 colour;
void main()
{
}
