#version 450
// shape: half width, half height, radius, thickness (large for a fill).
layout(location = 0) noperspective in vec2 local;
layout(location = 1) flat in vec4 shape;
layout(location = 2) noperspective in float t;
layout(location = 3) flat in vec4 top;
layout(location = 4) flat in vec4 bottom;
layout(location = 0) out vec4 frag;
void main()
{
	vec2 q = abs(local) - (shape.xy - shape.z);
	float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - shape.z;
	float coverage = clamp(0.5 - d, 0.0, 1.0) - clamp(0.5 - (d + shape.w), 0.0, 1.0);
	float w = floor(clamp(t, 0.0, 1.0) * 256.0);
	vec4 c = floor((top * (256.0 - w) + bottom * w) * (1.0 / 256.0));
	float a = c.a * (1.0 / 255.0) * coverage;
	frag = vec4(c.rgb * (a / 255.0), a);
}
