#version 450
// The colour to two attachments.
layout(location = 0) in vec3 shade;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
void main()
{
	color0 = vec4(shade, 1.0);
	color1 = vec4(shade.zyx, 0.5);
}
