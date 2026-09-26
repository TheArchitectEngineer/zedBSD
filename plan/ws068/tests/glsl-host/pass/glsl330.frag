#version 330 core
// GLSL 3.30 fragment shader: an in block of the same block name (another instance name), located outputs,
// texture functions of 1.30 and 3.30.
in VS_OUT {
	vec3 normal;
	flat int instance;
	noperspective float depth;
} fs_in;
layout(location = 0) out vec4 frag_color;
uniform sampler2D u_texture;
uniform sampler2DArray u_layers;
uniform sampler2DShadow u_shadow;

void main()
{
	vec3 n = normalize(fs_in.normal);
	vec4 texel = textureOffset(u_texture, n.xy, ivec2(0, 1));
	vec4 layer = textureLod(u_layers, vec3(n.xy, float(fs_in.instance)), 0.0);
	float lit = texture(u_shadow, vec3(n.xy, fs_in.depth));

	frag_color = (texel + layer) * lit * determinant(mat3(1.0));
}
