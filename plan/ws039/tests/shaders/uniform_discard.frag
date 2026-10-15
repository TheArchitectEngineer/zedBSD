#version 450
// ws039-p007: a discard of every pixel of the wave when a push constant says so (the early end).
layout(location = 0) in vec4 tint;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Push { float limit; } p;
void main()
{
	if (p.limit > 0.0)
		discard;
	color = tint * p.limit;
}
