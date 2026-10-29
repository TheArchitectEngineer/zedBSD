#version 450
#extension GL_ARB_shader_stencil_export : require
// The Mali compiler's host check: gl_FragStencilRefARB as the only output (no depth, no colour to blend).
layout(location = 0) in vec4 v;
void main()
{
	gl_FragStencilRefARB = int(v.x * 7.0) + 3;
}
