#version 330
#extension GL_ARB_tessellation_shader : require
// The levels and the patch's vertices read back as a colour; cw and fractional spacing given.
layout(quads, fractional_odd_spacing, cw) in;
in Corner {
	vec4 position;
} corners[];
out vec4 t_colour;
void main()
{
	vec4 below = mix(corners[0].position, corners[1].position, gl_TessCoord.x);
	vec4 above = mix(corners[2].position, corners[3].position, gl_TessCoord.x);
	t_colour = vec4(gl_TessLevelOuter[0] / 6.0, gl_TessLevelInner[1] / 6.0, float(gl_PatchVerticesIn) / 4.0, 1.0);
	gl_Position = mix(below, above, gl_TessCoord.y);
}
