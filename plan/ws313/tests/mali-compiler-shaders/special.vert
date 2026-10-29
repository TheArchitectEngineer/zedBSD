#version 450
// The Mali compiler's host check: the vertex and instance index, the point size, a uniform buffer and flat outputs.
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 coordinate;
layout(location = 0) out vec4 v;
layout(location = 1) flat out ivec2 f;
layout(location = 2) noperspective out vec2 n;
layout(set = 0, binding = 0) uniform U { mat4 transform; vec4 tint[4]; } u;
void main()
{
	gl_Position = u.transform * position;
	gl_PointSize = 1.0 + coordinate.x * coordinate.x;
	v = u.tint[gl_VertexIndex & 3] * coordinate.y + vec4(float(gl_InstanceIndex));
	f = ivec2(gl_VertexIndex, gl_InstanceIndex * 3 - 1);
	n = coordinate * 0.5;
}
