#version 450
// vkcompile: coarse and fine differences across the group of four of f = x * y / 256, times 4.
layout(location = 0) out vec4 color;
void main()
{
	float f = gl_FragCoord.x * gl_FragCoord.y * (1.0 / 256.0);
	color = vec4(dFdxCoarse(f), dFdxFine(f), dFdyCoarse(f), dFdyFine(f)) * 4.0;
}
