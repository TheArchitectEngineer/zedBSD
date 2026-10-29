#version 330 core
// expect: 0:3: error: layout(index) must be 0 or 1
layout(location = 0, index = 2) out vec4 colour;
void main()
{
	colour = vec4(1.0);
}
