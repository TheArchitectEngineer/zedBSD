#version 450
// vkcompile: a gather of the alpha channel moved by the texel offset (1, -1).
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) out vec4 color;
void main()
{
	vec2 at = (floor(gl_FragCoord.xy) + 1.0) / 16.0;
	color = textureGatherOffset(picture, at, ivec2(1, -1), 3);
}
