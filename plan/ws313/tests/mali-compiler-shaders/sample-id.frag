#version 450
// The Mali compiler's host check: gl_SampleID read into a colour, as a float and in integer arithmetic.
layout(location = 0) out vec4 color;
layout(push_constant) uniform P { int scale; } p;
void main()
{
	int id = gl_SampleID;
	color = vec4(float(id), float(id & 1), float(id * p.scale + 1), 1.0);
}
