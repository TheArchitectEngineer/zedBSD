#version 330
#extension GL_ARB_tessellation_shader : require
// GLSL 3.30 with GL_ARB_tessellation_shader: an output block per vertex, the levels from a uniform.
layout(vertices = 4) out;
uniform vec2 u_levels;
out Corner {
	vec4 position;
} corners[];
void main()
{
	corners[gl_InvocationID].position = gl_in[gl_InvocationID].gl_Position;
	gl_TessLevelOuter[0] = u_levels.x;
	gl_TessLevelOuter[1] = u_levels.x;
	gl_TessLevelOuter[2] = u_levels.x;
	gl_TessLevelOuter[3] = u_levels.x;
	gl_TessLevelInner[0] = u_levels.y;
	gl_TessLevelInner[1] = u_levels.y;
}
