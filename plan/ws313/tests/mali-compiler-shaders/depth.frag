#version 450
// The Mali compiler's host check: gl_FragDepth written with a colour, and a discard before it.
layout(location = 0) in vec4 v;
layout(location = 0) out vec4 color;
layout(push_constant) uniform P { float bias; } p;
void main()
{
	if (v.w > 1.9)
		discard;
	gl_FragDepth = clamp(v.z * 0.5 + p.bias, 0.0, 1.0);
	color = vec4(v.xy, gl_FragCoord.z, 0.25);
}
