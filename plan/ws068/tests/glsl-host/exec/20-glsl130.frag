// expect: 0 255 0 255
// version: 130
// GLSL 1.30: switch with fall-through and default, unsigned integers, bit operators, shifts, % and texture().
#version 130
// uniform: u_mode 2
// uniform: u_bits 12
uniform int u_mode;
uniform uint u_bits;
uniform sampler2D u_texture;
out vec4 color;

int pick(int mode)
{
	int result = 0;
	switch (mode) {
	case 0:
		result = 10;
		break;
	case 1:
	case 2:
		result += 20;
	case 3:
		result += 1;
		break;
	default:
		result = 99;
	}
	return result;
}

void main()
{
	int failed = 0;
	uint bits = u_bits;
	int x = -7;

	if (pick(u_mode) != 21) failed = 1;
	if (pick(0) != 10 || pick(3) != 1 || pick(9) != 99) failed = 2;
	if ((bits & 4u) != 4u || (bits | 1u) != 13u || (bits ^ 8u) != 4u) failed = 3;
	if ((bits >> 2u) != 3u || (bits << 1) != 24u) failed = 4;
	if (x % 3 != -1 || 7 % 3 != 1 || x / 2 != -3) failed = 5;
	if (~0 != -1 || (x >> 1) != -4) failed = 6;
	if (texture(u_texture, vec2(0.75, 0.25)) != vec4(0.0, 1.0, 0.0, 1.0)) failed = 7;
	if (textureSize(u_texture, 0) != ivec2(2, 2)) failed = 8;
	if (texelFetch(u_texture, ivec2(1, 1), 0) != vec4(1.0)) failed = 9;
	if (uint(u_mode) * 3u != 6u || u_bits / 5u != 2u || u_bits % 5u != 2u) failed = 10;
	switch (x) {
	case -7:
		x = 1;
		break;
	}
	if (x != 1) failed = 11;
	float f = 3;
	f += 1;
	if (f != 4.0 || max(f, 2) != 4.0) failed = 12;

	if (failed == 0)
		color = vec4(0.0, 1.0, 0.0, 1.0);
	else
		color = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
