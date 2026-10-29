#version 330 core
// expect: 0:3: error: layout(index) needs a layout(location) too
layout(index = 1) out vec4 colour;
void main()
{
	colour = vec4(1.0);
}
