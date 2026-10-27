#version 450
// Sixteen vec4 attributes: 64 registers, more than the 60 the fetcher writes.
layout(location = 0) in vec4 a0;
layout(location = 1) in vec4 a1;
layout(location = 2) in vec4 a2;
layout(location = 3) in vec4 a3;
layout(location = 4) in vec4 a4;
layout(location = 5) in vec4 a5;
layout(location = 6) in vec4 a6;
layout(location = 7) in vec4 a7;
layout(location = 8) in vec4 a8;
layout(location = 9) in vec4 a9;
layout(location = 10) in vec4 a10;
layout(location = 11) in vec4 a11;
layout(location = 12) in vec4 a12;
layout(location = 13) in vec4 a13;
layout(location = 14) in vec4 a14;
layout(location = 15) in vec4 a15;
layout(location = 0) out vec4 sum;
void main()
{
	gl_Position = a0 + a1 + a2 + a3;
	sum = a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15;
}
