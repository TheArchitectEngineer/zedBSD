#version 330 core
// GLSL 3.30 (desktop GL 3.3): the vertex stage of the dual-source blend pair.
layout(location = 0) in vec2 position;
out vec2 uv;

void main()
{
	uv = position * 0.5 + 0.5;
	gl_Position = vec4(position, 0.0, 1.0);
}
