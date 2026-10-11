#version 450
layout(set = 0, binding = 0) uniform sampler2D page;
layout(location = 0) noperspective in vec2 uv;
layout(location = 1) flat in vec4 color;
layout(location = 2) flat in float k;
layout(location = 0) out vec4 frag;
void main()
{
	vec4 texel = texture(page, uv);

	frag = color * texel.a + texel * k;
}
