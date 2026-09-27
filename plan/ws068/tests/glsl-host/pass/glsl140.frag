#version 140
in vec2 v_texel;
uniform sampler2DRect u_rect;
uniform usampler2DRect u_counts;
uniform sampler2DRectShadow u_depth;
uniform isamplerBuffer u_table;
out vec4 o_colour;
void main()
{
	ivec2 size = textureSize(u_rect);
	vec4 colour = texture(u_rect, v_texel) + textureProj(u_rect, vec3(v_texel, 1.0));
	uvec4 count = texelFetch(u_counts, ivec2(v_texel));
	float lit = texture(u_depth, vec3(v_texel, 0.5));
	int entry = texelFetch(u_table, int(count.x)).x;
	o_colour = colour * lit + vec4(float(entry) / float(size.x), 0.0, 0.0, 1.0);
}
