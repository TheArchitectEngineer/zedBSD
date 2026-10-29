#version 400
// A patch of the four corners: each vertex passed on, the levels and a patch colour by invocation 0.
layout(vertices = 4) out;
in vec2 v_position[];
out vec2 c_position[];
patch out vec4 p_colour;
void main()
{
	c_position[gl_InvocationID] = v_position[gl_InvocationID];
	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
	if (gl_InvocationID == 0) {
		gl_TessLevelOuter[0] = 2.0;
		gl_TessLevelOuter[1] = 2.0;
		gl_TessLevelOuter[2] = 2.0;
		gl_TessLevelOuter[3] = 2.0;
		gl_TessLevelInner[0] = 2.0;
		gl_TessLevelInner[1] = 2.0;
		p_colour = vec4(0.0, 1.0, 0.0, 1.0);
	}
	barrier();
}
