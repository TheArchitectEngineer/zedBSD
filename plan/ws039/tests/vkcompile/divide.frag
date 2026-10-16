#version 450
// vkcompile: unsigned division and remainder of a hashed numerator by a hashed divisor of every size, folded to 16
// bits each; the seed in the push constants.
layout(push_constant) uniform Push { vec4 rect; uvec4 param; } push;
layout(location = 0) out vec4 color;
uint hash(uint v)
{
	v ^= v >> 16;
	v *= 0x7feb352dU;
	v ^= v >> 15;
	v *= 0x846ca68bU;
	v ^= v >> 16;
	return v;
}
void main()
{
	uvec2 c = uvec2(gl_FragCoord.xy);
	uint a = hash(c.x + c.y * 64U + push.param.x * 4096U);
	uint b = (hash(a ^ 0x9e3779b9U) >> ((c.x + c.y) & 31U)) | 1U;
	uint q = a / b;
	uint r = a % b;
	q ^= q >> 16;
	r ^= r >> 16;
	color = vec4(float(q & 255U), float((q >> 8) & 255U), float(r & 255U), float((r >> 8) & 255U)) / 255.0;
}
