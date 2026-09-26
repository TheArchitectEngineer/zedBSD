#version 130
// expect: 0:3: error: 'layout' is a reserved word
layout(location = 0) in vec4 a;
void main()
{
	gl_Position = a;
}
