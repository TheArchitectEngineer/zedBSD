#version 450
// A dual-source blend's two colours of attachment 0.
layout(location = 0) in vec4 colour;
layout(location = 0, index = 0) out vec4 first;
layout(location = 0, index = 1) out vec4 second;
void main()
{
	first = colour;
	second = colour.wzyx * 0.5;
}
