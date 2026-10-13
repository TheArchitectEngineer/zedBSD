#version 450
// ws229-p003: a discarding shader writing render target 1 alone (the header carries the live pixels in word 15).
layout(location = 0) in vec4 color;
layout(location = 1) out vec4 out1;
void main()
{
	if (color.a < 0.5)
		discard;
	out1 = color;
}
