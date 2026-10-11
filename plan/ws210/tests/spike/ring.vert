#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 in_pixel;
layout(location = 1) in vec2 in_local;
layout(location = 2) in vec4 in_ring;
layout(location = 3) in vec2 in_more;
layout(location = 0) noperspective out vec2 out_local;
layout(location = 1) flat out vec4 out_ring;
layout(location = 2) flat out float out_half;
layout(location = 3) flat out vec4 out_color;
void main()
{
	gl_Position = place(in_pixel);
	out_local = in_local;
	out_ring = in_ring;
	out_half = in_more.x;
	out_color = unpack_color(in_more.y);
}
