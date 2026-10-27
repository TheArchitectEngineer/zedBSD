#version 450
// A sampled image, a uniform block, a storage buffer and push constants past byte 96.
layout(push_constant) uniform Push { vec4 a; layout(offset = 112) vec4 b; } push;
layout(set = 2, binding = 3) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform Block { vec4 k; } block;
layout(std430, set = 0, binding = 5) buffer Out { vec4 v[]; } dst;
layout(location = 0) in vec4 colour;
layout(location = 0) out vec4 result;
void main()
{
	result = texture(tex, colour.xy) * block.k + push.a + push.b;
	dst.v[1] = result;
}
