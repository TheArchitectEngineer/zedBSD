#version 140
in vec2 a_position;
uniform samplerBuffer u_offsets;
out vec2 v_texel;
void main()
{
	vec4 offset = texelFetch(u_offsets, gl_VertexID % textureSize(u_offsets));
	v_texel = (a_position * 0.5 + 0.5) * 64.0;
	gl_Position = vec4(a_position + offset.xy, 0.0, 1.0);
}
