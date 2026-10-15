#version 450
// ws039-p007: a pixel program: perspective, linear and flat attributes, the position and the face, two colours.
layout(location = 0) in vec2 coordinate;
layout(location = 1) flat in uint tag;
layout(location = 2) noperspective in vec4 tint;
layout(location = 0) out vec4 color;
layout(location = 2) out vec4 extra;
layout(push_constant) uniform Push { float gain; } p;
void main()
{
	float face = gl_FrontFacing ? 1.0 : -1.0;
	color = vec4(coordinate * p.gain, float(tag & 7u), face) + tint * 0.25;
	extra = vec4(gl_FragCoord.xy * 0.5, gl_FragCoord.z, gl_FragCoord.w);
}
