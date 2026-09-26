#version 130
// GLSL 1.30 fragment shader: a user output, texture(), texelFetch(), textureSize() and integer inputs.
in vec2 v_uv;
flat in int v_index;
noperspective in float v_depth;
out vec4 color;
uniform sampler2D u_texture;
uniform isampler2D u_indices;

void main()
{
	ivec2 size = textureSize(u_texture, 0);
	ivec4 fetched = texelFetch(u_indices, ivec2(v_uv * vec2(size)), 0);
	vec4 texel = texture(u_texture, v_uv);

	color = texel * float(fetched.x + v_index) + vec4(v_depth);
	color = clamp(color, 0.0, 1.0);
}
