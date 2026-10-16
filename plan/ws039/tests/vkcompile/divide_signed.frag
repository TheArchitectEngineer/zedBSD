#version 450
// vkcompile: signed division and remainder (truncated) of hashed operands of both signs, folded to 16 bits each.
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
	uint h = hash(c.x * 131U + c.y * 7919U + push.param.x * 65537U);
	int a = int(h & 0x7fffffffU);
	if ((h & 0x80000000U) != 0U)
		a = -a;
	uint g = hash(h ^ 0x85ebca6bU);
	int b = int(((g >> ((c.x ^ c.y) & 31U)) | 1U) & 0x7fffffffU);
	if ((g & 1U) != 0U)
		b = -b;
	uint q = uint(a / b);
	uint r = uint(a % b);
	q ^= q >> 16;
	r ^= r >> 16;
	color = vec4(float(q & 255U), float((q >> 8) & 255U), float(r & 255U), float((r >> 8) & 255U)) / 255.0;
}
