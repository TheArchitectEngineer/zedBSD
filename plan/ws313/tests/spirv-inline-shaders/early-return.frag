#version 450
// A helper that returns before its end: the inlining refuses it.
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o_color;

float positive(float x)
{
	if (x < 0.0)
		return 0.0;
	return x;
}

void main()
{
	o_color = vec4(positive(v_color.x));
}
