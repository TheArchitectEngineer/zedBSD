#version 330
// expect: 0:3: error: layout(location) is only for vertex shader inputs and fragment shader outputs
layout(location = 2) uniform vec4 u;
void main()
{
	gl_Position = u;
}
