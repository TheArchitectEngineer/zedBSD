#version 450
// ws039-p007: discards, one of them inside a loop whose trip count differs between pixels.
layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(location = 0) out vec4 color;
void main()
{
	if (coordinate.x > 0.75)
		discard;
	vec4 c = tint;
	for (int i = 0; i < int(coordinate.y * 6.0); i++) {
		c = c * 0.5 + vec4(0.125);
		if (c.x < tint.y - 0.5)
			discard;
	}
	color = c;
}
