#version 450
// Differences across the pixel's group of four: coarse and fine, along x and y, of interpolated inputs, of a product
// of them, and of a push constant (no difference); fwidth.
layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(push_constant) uniform Push { vec4 scale; } push;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 more;
layout(location = 2) out vec4 rest;
void main()
{
	float product = coordinate.x * tint.y + tint.z * tint.z;
	color = vec4(dFdx(coordinate.x), dFdy(coordinate.y), dFdxCoarse(product), dFdyCoarse(product));
	more = vec4(dFdxFine(tint.x), dFdyFine(tint.w), dFdxFine(product), dFdyFine(product));
	rest = vec4(fwidth(coordinate), dFdx(push.scale.x), fwidthFine(product) + push.scale.y);
}
