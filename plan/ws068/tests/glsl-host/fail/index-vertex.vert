#version 330 core
// expect: 0:3: error: layout(index) is only for fragment shader outputs
layout(location = 0, index = 1) in vec4 a;
void main()
{
	gl_Position = a;
}
