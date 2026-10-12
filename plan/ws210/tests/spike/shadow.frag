#version 450
// shape: half width, half height, radius, softness (at least 1).
layout(location = 0) noperspective in vec2 local;
layout(location = 1) flat in vec4 shape;
layout(location = 2) flat in vec4 color;
layout(location = 0) out vec4 frag;
void main()
{
	vec2 q = abs(local) - (shape.xy - shape.z);
	float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - shape.z;
	float amount = clamp((d + shape.w) / (2.0 * shape.w), 0.0, 1.0);
	float coverage = 1.0 - amount * amount * (3.0 - 2.0 * amount);
	float a = color.a * (1.0 / 255.0) * coverage;
	frag = vec4(color.rgb * (a / 255.0), a);
}
