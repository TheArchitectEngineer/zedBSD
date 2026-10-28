#version 450
// The interpolated colour, opaque.
layout(location = 0) in vec3 shade;
layout(location = 0) out vec4 color;
void main()
{
	color = vec4(shade, 1.0);
}
