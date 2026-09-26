#version 300 es
precision mediump float;
in vec4 v_color;
in vec2 v_uv;
uniform float u_textured;
uniform sampler2D u_texture;
layout(location = 0) out vec4 color;

void main()
{
	vec4 texel = texture(u_texture, v_uv);

	color = u_textured > 0.5 ? v_color * texel : v_color;
}
