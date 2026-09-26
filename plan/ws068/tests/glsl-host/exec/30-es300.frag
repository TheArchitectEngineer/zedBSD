// expect: 0 255 0 255
// version: 300
#version 300 es
// OpenGL ES 3.00: integers, bit casts, packing, matrices, texture functions and located outputs.
precision highp float;
uniform sampler2D u_texture;
uniform highp isampler2D u_unused;
flat in int v_instance;
flat in uvec2 v_ids;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 unused;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

void main()
{
	int failed = 0;
	uint u = 0xf0u;
	mat2 m = mat2(1.0, 2.0, 3.0, 4.0);
	mat2 inv = inverse(m);

	if (floatBitsToInt(1.0) != 0x3f800000) failed = 1;
	if (!near(intBitsToFloat(0x40000000), 2.0) || !near(uintBitsToFloat(0x3f000000u), 0.5)) failed = 2;
	if (packHalf2x16(vec2(1.0, -2.0)) != 0xc0003c00u) failed = 3;
	if (unpackHalf2x16(0x3c00c000u) != vec2(-2.0, 1.0)) failed = 4;
	if (packUnorm2x16(vec2(0.0, 1.0)) != 0xffff0000u || packSnorm2x16(vec2(-1.0, 0.0)) != 0x00008001u) failed = 5;
	if (!near(determinant(m), -2.0)) failed = 6;
	if (!near(inv[0][0], -2.0) || !near(inv[1][0], 1.5)) failed = 7;
	if ((u >> 4) != 15u || (u & 0x30u) != 0x30u || u % 7u != 2u) failed = 8;
	if (v_instance != 0 || v_ids != uvec2(3u, 4u)) failed = 9;
	if (!near(texture(u_texture, vec2(0.75, 0.25)), vec4(0.0, 1.0, 0.0, 1.0))) failed = 10;
	if (!near(textureLod(u_texture, vec2(0.25, 0.75), 0.0), vec4(0.0, 0.0, 1.0, 1.0))) failed = 11;
	if (!near(textureGrad(u_texture, vec2(0.75, 0.75), vec2(0.0), vec2(0.0)), vec4(1.0))) failed = 12;
	if (!near(textureOffset(u_texture, vec2(0.25, 0.25), ivec2(1, 0)), vec4(0.0, 1.0, 0.0, 1.0))) failed = 13;
	if (!near(texelFetch(u_texture, ivec2(0, 1), 0), vec4(0.0, 0.0, 1.0, 1.0))) failed = 14;
	if (textureSize(u_texture, 0) != ivec2(2)) failed = 15;
	if (failed == 0)
		color = vec4(0.0, 1.0, 0.0, 1.0);
	else
		color = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
	unused = vec4(0.0);
}
