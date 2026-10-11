#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 in_pixel;
layout(location = 1) in vec2 in_local;
layout(location = 2) in vec4 in_shape;
layout(location = 3) in float in_color;
layout(location = 0) noperspective out vec2 out_local;
layout(location = 1) flat out vec4 out_shape;
layout(location = 2) flat out vec4 out_color;
void main()
{
	gl_Position = place(in_pixel);
	out_local = in_local;
	out_shape = in_shape;
	out_color = unpack_color(in_color);
}
