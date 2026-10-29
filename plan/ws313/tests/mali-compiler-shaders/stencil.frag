#version 450
#extension GL_ARB_shader_stencil_export : require
// The Mali compiler's host check: gl_FragStencilRefARB written with a colour (no depth).
layout(location = 0) in vec4 v;
layout(location = 0) out vec4 color;
layout(push_constant) uniform P { int base; } p;
void main()
{
	gl_FragStencilRefARB = p.base + int(v.x * 100.0);
	color = vec4(v.yz, 0.5, 1.0);
}
