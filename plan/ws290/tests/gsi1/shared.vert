#version 450
// The vertex program of a pipeline whose fragment program shares its texture and uniform block.
layout(set = 0, binding = 0) uniform sampler2D heights;
layout(set = 0, binding = 1) uniform Block { vec4 scale; } block;
layout(location = 0) in vec4 position;
layout(location = 0) out vec4 colour;
void main()
{
	gl_Position = position * block.scale + textureLod(heights, position.xy, 0.0);
	colour = position;
}
