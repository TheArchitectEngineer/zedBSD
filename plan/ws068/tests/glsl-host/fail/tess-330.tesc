#version 330
// expect: error: tessellation shaders need GLSL 4.00 or GL_ARB_tessellation_shader
layout(vertices = 3) out;
void main()
{
}
