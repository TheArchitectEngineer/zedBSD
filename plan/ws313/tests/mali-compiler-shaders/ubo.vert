#version 450
// The Mali compiler's host check: runs of uniform buffer words at offsets of every alignment, a vec3, a matrix and a far offset.
layout(location = 0) in vec4 position;
layout(location = 0) out vec4 shade;
layout(location = 1) out vec4 runs;
layout(set = 0, binding = 0, std140) uniform U {
	float a;
	float s;
	vec2 p;
	vec2 q;
	vec3 c;
	float d;
	vec4 e;
	mat4 m;
	vec4 far[2100];
} u;
void main()
{
	gl_Position = u.m * position + u.e;
	shade = vec4(vec3(u.s, u.p), u.d) + u.far[2050];
	runs = vec4(u.p, u.q) + vec4(u.c, u.a);
}
