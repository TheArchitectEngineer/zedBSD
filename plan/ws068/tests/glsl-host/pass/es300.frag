#version 300 es
// OpenGL ES 3.00 fragment shader: two located outputs, texture(), textureLod(), textureGrad(), textureOffset(),
// texelFetch(), textureSize(), an array texture, a cube shadow lookup, bit casts, packing, inverse and determinant.
precision highp float;
precision highp sampler2DArray;
in vec2 v_uv;
flat in int v_index;
flat in uvec2 v_ids;
centroid in float v_depth;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 extra;
uniform sampler2D u_texture;
uniform highp sampler2DArray u_layers;
uniform highp samplerCubeShadow u_shadow;
uniform highp isampler2D u_indices;

void main()
{
	vec4 texel = texture(u_texture, v_uv);
	vec4 level = textureLod(u_texture, v_uv, 1.0);
	vec4 gradient = textureGrad(u_texture, v_uv, vec2(0.01), vec2(0.01));
	vec4 moved = textureOffset(u_texture, v_uv, ivec2(1, -1));
	vec4 layer = texture(u_layers, vec3(v_uv, 2.0));
	float lit = texture(u_shadow, vec4(normalize(vec3(v_uv, 1.0)), v_depth));
	ivec4 fetched = texelFetch(u_indices, ivec2(v_uv * vec2(textureSize(u_indices, 0))), 0);
	mat2 m = inverse(mat2(2.0, 0.0, 0.0, 4.0));
	uint half_pair = packHalf2x16(vec2(1.0, -2.0));
	int bits = floatBitsToInt(texel.x);

	color = texel + level + gradient + moved + layer * lit + vec4(float(fetched.x + v_index + bits % 2));
	color *= determinant(m) + unpackHalf2x16(half_pair).x + float(v_ids.x);
	extra = vec4(intBitsToFloat(bits), uintBitsToFloat(v_ids.y), 0.0, 1.0);
}
