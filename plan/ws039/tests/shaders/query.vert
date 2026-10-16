#version 450
// ws039-p007: samples with gradients, a depth compare at a level and a size query in a vertex program.
layout(location = 0) in vec4 position;
layout(set = 0, binding = 0) uniform sampler2D colors;
layout(set = 0, binding = 1) uniform sampler2DShadow depths;
layout(location = 0) out vec4 value;
layout(location = 1) out vec4 other;
void main()
{
	value = textureGrad(colors, position.xy, position.zw * 0.25, position.wz * 0.5);
	float s = textureLod(depths, vec3(position.yx, position.z * 0.25 + 0.5), 0.0);
	ivec2 size = textureSize(colors, 0);
	other = vec4(s, float(size.x), float(size.y), 1.0);
	gl_Position = position;
}
