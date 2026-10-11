#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 in_pixel;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_more;
layout(location = 0) noperspective out vec2 out_uv;
layout(location = 1) flat out vec4 out_color;
layout(location = 2) flat out float out_k;
void main()
{
	vec4 c = unpack_color(in_more.x) * (1.0 / 255.0);

	gl_Position = place(in_pixel);
	out_uv = in_uv;
	out_color = vec4(c.rgb * c.a, c.a);
	out_k = in_more.y;
}
