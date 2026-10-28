#version 450
// A triangle's vertex: a 2D position and a colour from two attributes.
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
layout(location = 0) out vec3 shade;
void main()
{
	gl_Position = vec4(position, 0.0, 1.0);
	shade = color;
}
