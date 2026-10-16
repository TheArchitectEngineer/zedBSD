#version 450
// vkcompile: the size of the sampled image and its level count.
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) out vec4 color;
void main()
{
	ivec2 s = textureSize(picture, 0);
	color = vec4(float(s.x), float(s.y), float(textureQueryLevels(picture)), 0.0) / 255.0;
}
