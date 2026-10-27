#version 450
// Eight colour attachments.
layout(location = 0) in vec4 colour;
layout(location = 0) out vec4 out0;
layout(location = 1) out vec4 out1;
layout(location = 2) out vec4 out2;
layout(location = 3) out vec4 out3;
layout(location = 4) out vec4 out4;
layout(location = 5) out vec4 out5;
layout(location = 6) out vec4 out6;
layout(location = 7) out vec4 out7;
void main()
{
	out0 = colour * 1.0;
	out1 = colour * 2.0;
	out2 = colour * 3.0;
	out3 = colour * 4.0;
	out4 = colour * 5.0;
	out5 = colour * 6.0;
	out6 = colour * 7.0;
	out7 = colour * 8.0;
}
