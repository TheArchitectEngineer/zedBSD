#version 450
// vkcompile: discards of whole 4x4 blocks, of single pixels of a group of four, and of the whole lower right quarter
// (every pixel of its waves, which then end early).
layout(location = 0) out vec4 color;
void main()
{
	uvec2 c = uvec2(gl_FragCoord.xy);
	if (c.x >= 32U && c.y >= 32U)
		discard;
	if ((((c.x >> 2) + (c.y >> 2)) & 1U) == 1U)
		discard;
	if ((c.y & 7U) == 7U && (c.x & 1U) == 1U)
		discard;
	color = vec4(float(c.x) / 255.0, float(c.y) / 255.0, 0.5, 1.0);
}
