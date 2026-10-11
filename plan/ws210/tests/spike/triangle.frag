#version 450
// edges: the signed distances to the three edges (positive inside).
layout(location = 0) noperspective in vec3 edges;
layout(location = 1) flat in vec4 color;
layout(location = 0) out vec4 frag;
void main()
{
	frag = color * clamp(0.5 + min(edges.x, min(edges.y, edges.z)), 0.0, 1.0);
}
