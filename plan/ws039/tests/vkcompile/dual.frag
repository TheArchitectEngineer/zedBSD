#version 450
// vkcompile: two colours for a dual-source blend: the colour and the factor the target is weighed by.
layout(location = 0, index = 0) out vec4 color;
layout(location = 0, index = 1) out vec4 factor;
void main()
{
	uvec2 c = uvec2(gl_FragCoord.xy);
	color = vec4(float(c.x) / 255.0, float(c.y) / 255.0, 0.25, 0.5);
	factor = vec4(0.5, 0.25, 1.0, 0.0);
}
