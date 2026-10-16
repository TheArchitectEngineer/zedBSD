#version 450
// ws039-p007: depth compares (implicit, with a bias, at a level, with gradients), samples with gradients (with an
// offset), and size and level queries in a pixel program.
layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 extra;
layout(set = 0, binding = 0) uniform sampler2D colors;
layout(set = 0, binding = 1) uniform sampler2DShadow depths;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 more;
void main()
{
	vec2 dx = extra.xy * 0.125;
	vec2 dy = extra.zw * 0.0625;
	vec4 a = textureGrad(colors, coordinate, dx, dy);
	vec4 b = textureGradOffset(colors, coordinate.yx, dy, dx, ivec2(2, -1));
	float s0 = texture(depths, vec3(coordinate, extra.x * 0.25 + 0.5));
	float s1 = texture(depths, vec3(coordinate.yx, extra.y * 0.25 + 0.5), 0.75);
	float s2 = textureLod(depths, vec3(coordinate, extra.z * 0.25 + 0.5), 0.0);
	float s3 = textureGrad(depths, vec3(coordinate, extra.w * 0.25 + 0.5), dx, dy);
	float s4 = textureOffset(depths, vec3(coordinate, 0.5), ivec2(-1, 1));
	ivec2 size = textureSize(colors, 0);
	int levels = textureQueryLevels(colors);
	color = a + b;
	more = vec4(s0 + s4 * 2.0, s1, s2 + s3 * 4.0, float(size.x + size.y * 3 + levels * 7));
}
