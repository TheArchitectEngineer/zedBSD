#version 450
// The Mali compiler's host check: gl_SampleMaskIn read, and gl_SampleMask written from it and a push constant.
layout(location = 0) out vec4 color;
layout(push_constant) uniform P { int keep; } p;
void main()
{
	int covered = gl_SampleMaskIn[0];
	gl_SampleMask[0] = covered & (p.keep | 0x5);
	color = vec4(float(covered), float(covered & 1), 0.0, 1.0);
}
