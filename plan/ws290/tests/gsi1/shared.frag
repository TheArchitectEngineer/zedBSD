#version 450
// The fragment program of the pipeline: the vertex program's texture and block, one texture and a buffer more.
layout(set = 0, binding = 0) uniform sampler2D heights;
layout(set = 0, binding = 2) uniform sampler2D albedo;
layout(set = 0, binding = 1) uniform Block { vec4 scale; } block;
layout(std430, set = 1, binding = 0) buffer Out { vec4 v[]; } dst;
layout(location = 0) in vec4 colour;
layout(location = 0) out vec4 result;
void main()
{
	result = texture(albedo, colour.xy) * block.scale + texture(heights, colour.zw);
	dst.v[0] = result;
}
