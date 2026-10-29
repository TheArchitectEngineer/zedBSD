#version 450
// The Mali compiler's host check: gl_FragDepth as the only output (no colour to blend).
layout(location = 0) in vec4 v;
void main()
{
	gl_FragDepth = v.x * v.y * 0.25;
}
