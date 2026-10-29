#version 400
// expect: 0:4: error: the output array's size is not the 3 output vertices
layout(vertices = 3) out;
out vec4 colour[4];
void main()
{
}
