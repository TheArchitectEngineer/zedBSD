#version 450
// The sample count of a multisampled image (texsz.s).
layout(location = 0) in vec3 shade;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2DMS image;
void main()
{
	color = vec4(shade, float(textureSamples(image)));
}
