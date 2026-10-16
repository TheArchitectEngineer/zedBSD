#version 450
// vkcompile: a gather of the green channel at the corner shared by texels (x, y) and (x + 1, y + 1) of a 16x16 image.
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) out vec4 color;
void main()
{
	vec2 at = (floor(gl_FragCoord.xy) + 1.0) / 16.0;
	color = textureGather(picture, at, 1);
}
