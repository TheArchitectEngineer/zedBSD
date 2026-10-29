#version 450
// A vertex shader whose position and colour come from helper functions reading a uniform buffer.
layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec4 a_col;
layout(location = 0) out vec4 v_col;
layout(set = 0, binding = 0) uniform U {
	mat4 m;
	vec4 bias;
} u;

vec4 xform(vec4 p)
{
	return u.m * p;
}

vec4 clampc(vec4 c)
{
	return clamp(c, 0.0, 1.0);
}

void main()
{
	gl_Position = xform(a_pos) + u.bias;
	v_col = clampc(a_col * 2.0);
}
