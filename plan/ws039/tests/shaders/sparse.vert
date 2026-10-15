#version 450
// ws039-p007: a vertex program with a loop, a parameter written in it, and no point size.
layout(location = 2) in vec4 a;
layout(location = 5) in vec3 b;
layout(location = 3) out vec4 sum;
void main()
{
	vec4 s = vec4(0.0);
	for (int i = 0; i < int(abs(b.x)) % 5; i++)
		s += a * float(i + 1);
	sum = s + vec4(b, 1.0);
	gl_Position = vec4(a.xy, b.z, 1.0);
}
