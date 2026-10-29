#version 450
#extension GL_ARB_shader_stencil_export : require
// The Mali compiler's host check: gl_FragStencilRefARB and gl_FragDepth written with a colour, and a discard before them.
layout(location = 0) in vec4 v;
layout(location = 0) out vec4 color;
layout(push_constant) uniform P { int base; float bias; } p;
void main()
{
	if (v.w > 1.9)
		discard;
	gl_FragDepth = clamp(v.z * 0.5 + p.bias, 0.0, 1.0);
	gl_FragStencilRefARB = (p.base ^ int(v.y * 50.0)) & 0xFF;
	color = vec4(v.xy, 0.25, 1.0);
}
