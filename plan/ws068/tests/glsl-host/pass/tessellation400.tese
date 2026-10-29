#version 400
// The quad domain over the four corners (a strip's order: 0 1 below, 2 3 above).
layout(quads, equal_spacing, ccw) in;
in vec2 c_position[];
patch in vec4 p_colour;
out vec4 t_colour;
void main()
{
	vec2 below = mix(c_position[0], c_position[1], gl_TessCoord.x);
	vec2 above = mix(c_position[2], c_position[3], gl_TessCoord.x);
	t_colour = p_colour;
	gl_Position = vec4(mix(below, above, gl_TessCoord.y), 0.0, 1.0);
}
