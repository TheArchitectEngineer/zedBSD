#version 450
// ws039-p007: a vertex program: fetched locations of 4, 2 and 1 channels and an integer one, the vertex and instance
// numbers, a uniform buffer, the position, the point size and parameters (one of them flat).
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 coordinate;
layout(location = 3) in float weight;
layout(location = 4) in uvec4 tag;
layout(set = 0, binding = 0) uniform Frame { vec4 scale; vec4 offset; } frame;
layout(push_constant) uniform Push { float size; } p;
layout(location = 0) out vec2 out_coordinate;
layout(location = 1) flat out uint out_tag;
layout(location = 2) out vec4 out_color;
void main()
{
	gl_Position = position * frame.scale + frame.offset + vec4(weight, 0.0, 0.0, 0.0);
	gl_PointSize = p.size + float(gl_VertexIndex & 3);
	out_coordinate = coordinate * 0.5 + vec2(float(gl_InstanceIndex), 1.0);
	out_tag = tag.x + tag.w * 16u + uint(gl_VertexIndex);
	out_color = vec4(position.zw, coordinate.y, weight);
}
