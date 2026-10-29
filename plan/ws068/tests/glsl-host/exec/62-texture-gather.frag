#version 330
#extension GL_ARB_texture_gather : require
// expect: 0 255 0 255
// version: 330
// GL_ARB_texture_gather: the red of the four texels around the centre (i0 j1, i1 j1, i1 j0, i0 j0), and with an
// offset of -1 (each texel clamped to the first).
uniform sampler2D u_texture;
out vec4 colour;

void main()
{
	int failed = 0;

	if (textureGather(u_texture, vec2(0.5, 0.5)) != vec4(0.0, 1.0, 0.0, 1.0)) failed = 1;
	if (textureGatherOffset(u_texture, vec2(0.5, 0.5), ivec2(-1, -1)) != vec4(1.0)) failed = 2;

	if (failed == 0)
		colour = vec4(0.0, 1.0, 0.0, 1.0);
	else
		colour = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
