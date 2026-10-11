// WS210 p002 spike: shared helpers for the candidate GPU canvas shaders (not product code).
// Pixel position to clip space; the push constant is (2 / width, 2 / height).
layout(push_constant) uniform Frame {
	vec2 scale;
} frame;

vec4 place(vec2 pixel)
{
	return vec4(pixel * frame.scale - 1.0, 0.0, 1.0);
}

// A 0xAARRGGBB colour carried in a float's bits, as 0..255 straight channels (r, g, b, a).
vec4 unpack_color(float bits)
{
	uint word = floatBitsToUint(bits);

	return vec4(float((word >> 16) & 255u), float((word >> 8) & 255u), float(word & 255u), float(word >> 24));
}
