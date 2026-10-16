#version 450
// Gathers of each channel, with an offset, of a depth compare, of an array of 2D images and of a cube.
layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 extra;
layout(set = 0, binding = 0) uniform sampler2D colors;
layout(set = 0, binding = 1) uniform sampler2DShadow depths;
layout(set = 0, binding = 2) uniform sampler2DArray layers;
layout(set = 0, binding = 3) uniform samplerCube cube;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 more;
layout(location = 2) out vec4 rest;
void main()
{
	vec4 a = textureGather(colors, coordinate);
	vec4 b = textureGather(colors, coordinate.yx, 1);
	vec4 c = textureGather(colors, coordinate, 2);
	vec4 d = textureGatherOffset(colors, coordinate, ivec2(-2, 3), 3);
	vec4 e = textureGather(depths, coordinate, extra.x * 0.25 + 0.5);
	vec4 f = textureGatherOffset(depths, coordinate.yx, extra.y * 0.25 + 0.5, ivec2(1, 1));
	vec4 g = textureGather(layers, vec3(coordinate, extra.z + 1.0), 1);
	vec4 h = textureGather(cube, vec3(coordinate, extra.w), 2);
	color = a + b * 0.5 + c * 0.25;
	more = d + e * 0.5 + f * 0.25;
	rest = g + h * 0.5;
}
