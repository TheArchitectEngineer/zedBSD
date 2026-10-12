#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 in_pixel;
layout(location = 1) in vec2 in_local;
layout(location = 2) in vec4 in_shape;
layout(location = 3) in float in_t;
layout(location = 4) in vec2 in_colors;
layout(location = 0) noperspective out vec2 out_local;
layout(location = 1) flat out vec4 out_shape;
layout(location = 2) noperspective out float out_t;
layout(location = 3) flat out vec4 out_top;
layout(location = 4) flat out vec4 out_bottom;
void main()
{
	gl_Position = place(in_pixel);
	out_local = in_local;
	out_shape = in_shape;
	out_t = in_t;
	out_top = unpack_color(in_colors.x);
	out_bottom = unpack_color(in_colors.y);
}
