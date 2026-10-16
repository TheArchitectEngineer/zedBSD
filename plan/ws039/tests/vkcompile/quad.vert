#version 450
// vkcompile: a corner in [0, 1] placed in the rectangle of the push constants (normalized device coordinates); points
// are 8 pixels wide.
layout(location = 0) in vec2 corner;
layout(push_constant) uniform Push { vec4 rect; vec4 param; } push;
void main()
{
	gl_Position = vec4(mix(push.rect.xy, push.rect.zw, corner), 0.0, 1.0);
	gl_PointSize = 8.0;
}
