#version 450
layout(location = 0) noperspective in float t;
layout(location = 1) flat in vec4 top;
layout(location = 2) flat in vec4 bottom;
layout(location = 0) out vec4 frag;
void main()
{
	float w = floor(clamp(t, 0.0, 1.0) * 256.0);
	vec4 c = floor((top * (256.0 - w) + bottom * w) * (1.0 / 256.0));
	float a = c.a * (1.0 / 255.0);
	frag = vec4(c.rgb * (a / 255.0), a);
}
