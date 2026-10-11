#version 450
// ring: radius, thickness, the end's direction (sin, -cos of the angle); half is 1 past half a turn.
layout(location = 0) noperspective in vec2 local;
layout(location = 1) flat in vec4 ring;
layout(location = 2) flat in float half_turn;
layout(location = 3) flat in vec4 color;
layout(location = 0) out vec4 frag;
void main()
{
	float distance = length(local);
	float band = clamp(0.5 - (distance - ring.x), 0.0, 1.0) - clamp(0.5 - (distance - (ring.x - ring.y)), 0.0, 1.0);
	float start = step(0.0, local.x);
	float end = step(0.0, local.x * ring.w - local.y * ring.z);
	float inside = mix(start * end, max(start, end), half_turn);
	float a = color.a * (1.0 / 255.0) * band * inside;
	frag = vec4(color.rgb * (a / 255.0), a);
}
