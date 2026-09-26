#version 300 es
// OpenGL ES 3.00: in and out, layout locations, flat integers, gl_InstanceID and gl_VertexID, uint, switch.
layout(location = 0) in vec4 a_position;
layout(location = 3) in vec2 a_uv;
in uvec2 a_ids;
out vec2 v_uv;
flat out int v_index;
flat out uvec2 v_ids;
centroid out float v_depth;
uniform mat4 u_matrix;

void main()
{
	int mode = gl_InstanceID + gl_VertexID % 3;
	float scale = 1.0;

	switch (mode) {
	case 0:
		scale = 2.0;
		break;
	default:
		scale = 0.5;
	}
	v_uv = a_uv * scale;
	v_index = mode;
	v_ids = a_ids ^ uvec2(1u, 2u);
	v_depth = a_position.z;
	gl_Position = u_matrix * a_position;
}
