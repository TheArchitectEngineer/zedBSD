#version 450
// ws039-p007: a sample in a vertex program (level 0, and at a level with an offset).
layout(location = 0) in vec4 position;
layout(set = 0, binding = 2) uniform sampler2D height;
layout(location = 0) out vec4 value;
void main()
{
	vec4 h = texture(height, position.xy);
	value = h + textureLodOffset(height, position.zw, 0.0, ivec2(-1, 3));
	gl_Position = position + vec4(0.0, h.x, 0.0, 0.0);
}
