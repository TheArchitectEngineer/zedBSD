#version 450
// A uniform buffer, a combined image sampler and a uniform texel buffer of set 0.
layout(location = 0) in vec3 shade;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform Tint { vec4 tint; };
layout(set = 0, binding = 1) uniform sampler2D image;
layout(set = 0, binding = 2) uniform samplerBuffer texels;
void main()
{
	color = texture(image, shade.xy) * tint + texelFetch(texels, 3);
}
