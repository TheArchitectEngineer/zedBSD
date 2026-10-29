#version 450
// Helper functions of one block (nested), of several blocks (an if, a loop), an inout parameter and a call in an if
// and in a loop body: compiled without -O they stay functions, and the parse inlines them.
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(push_constant) uniform P {
	vec4 tint;
	float k;
	int n;
} pc;

float sq(float x)
{
	return x * x;
}

vec4 scale(vec4 c, float s)
{
	return c * sq(s);
}

float absval(float x)
{
	float r;
	if (x > 0.0)
		r = x;
	else
		r = -x;
	return r;
}

void accumulate(inout vec4 acc, vec4 add)
{
	acc += add;
}

float sum_loop(int n)
{
	float s = 0.0;
	for (int i = 0; i < n; i++)
		s += float(i) * 0.5;
	return s;
}

void main()
{
	vec4 acc = vec4(0.0);
	accumulate(acc, scale(v_color, pc.k));
	accumulate(acc, vec4(absval(v_uv.x - 0.5), absval(v_uv.y), 0.0, 0.0));
	if (v_uv.x > 0.25)
		accumulate(acc, pc.tint);
	for (int i = 0; i < 3; i++) {
		acc.x += sq(float(i));
		acc.y += absval(acc.y - 1.0);
	}
	acc.w += sum_loop(pc.n & 3);
	o_color = acc;
}
