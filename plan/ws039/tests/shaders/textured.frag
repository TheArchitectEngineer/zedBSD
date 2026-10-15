#version 450
// ws039-p007: samples in a pixel program: with derivatives, with a constant offset, at a level, with a bias, and a
// texel fetch, from two sampled images, after a discard.
layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(set = 0, binding = 1) uniform sampler2D first;
layout(set = 1, binding = 0) uniform sampler2D second;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 more;
void main()
{
	if (tint.w > 3.5)
		discard;
	vec4 a = texture(first, coordinate);
	vec4 b = textureOffset(second, coordinate * 2.0, ivec2(1, -2));
	vec4 c = textureLod(first, coordinate.yx, 0.0);
	vec4 d = texture(second, coordinate, 0.5);
	vec4 e = texelFetch(first, ivec2(int(abs(tint.x) * 3.0) & 7, int(abs(tint.y) * 2.0) & 3), 0);
	color = a * tint + b;
	more = c + d * 0.5 + e;
}
