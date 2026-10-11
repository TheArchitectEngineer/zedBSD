#version 450
// shape: half width, half height, radius, opacity.
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) noperspective in vec2 uv;
layout(location = 1) noperspective in vec2 local;
layout(location = 2) flat in vec4 shape;
layout(location = 0) out vec4 frag;
void main()
{
	vec2 q = abs(local) - (shape.xy - shape.z);
	float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - shape.z;

	frag = texture(picture, uv) * (shape.w * clamp(0.5 - d, 0.0, 1.0));
}
