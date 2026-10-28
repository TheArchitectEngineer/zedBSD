#version 450
// ws229-p003: three render targets, location 0, 1 and 3 (Gen9 names each target in a message header).
layout(location = 0) in vec4 color;
layout(location = 0) out vec4 out0;
layout(location = 1) out vec4 out1;
layout(location = 3) out vec4 out3;
void main()
{
	out0 = color;
	out1 = color.wzyx;
	out3 = vec4(color.x * 0.5, 0.25, 0.0, 1.0);
}
