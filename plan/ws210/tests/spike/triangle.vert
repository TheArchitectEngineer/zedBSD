#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 in_pixel;
layout(location = 1) in vec4 in_edges;
layout(location = 0) noperspective out vec3 out_edges;
layout(location = 1) flat out vec4 out_color;
void main()
{
	vec4 c = unpack_color(in_edges.w) * (1.0 / 255.0);

	gl_Position = place(in_pixel);
	out_edges = in_edges.xyz;
	out_color = vec4(c.rgb * c.a, c.a);
}
