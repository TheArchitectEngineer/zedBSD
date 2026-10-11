#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 in_pixel;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_local;
layout(location = 3) in vec4 in_shape;
layout(location = 0) noperspective out vec2 out_uv;
layout(location = 1) noperspective out vec2 out_local;
layout(location = 2) flat out vec4 out_shape;
void main()
{
	gl_Position = place(in_pixel);
	out_uv = in_uv;
	out_local = in_local;
	out_shape = in_shape;
}
