#version 450
// vkcompile: the primitive number, its half in red and its lowest bit in green.
layout(location = 0) out vec4 color;
void main()
{
	color = vec4(float(gl_PrimitiveID >> 1) / 255.0, float(gl_PrimitiveID & 1), 0.0, 1.0);
}
