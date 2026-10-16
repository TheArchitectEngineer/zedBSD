#version 450
// vkcompile: the coordinate of the pixel in its point sprite.
layout(location = 0) out vec4 color;
void main()
{
	color = vec4(gl_PointCoord, 0.0, 1.0);
}
