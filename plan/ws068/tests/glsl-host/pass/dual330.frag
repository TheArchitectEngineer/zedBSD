#version 330 core
// GLSL 3.30 (desktop GL 3.3): a dual-source blend's two colours at location 0, layout(index).
in vec2 uv;
layout(location = 0, index = 0) out vec4 colour;
layout(location = 0, index = 1) out vec4 factor;

void main()
{
	colour = vec4(uv, 0.0, 1.0);
	factor = vec4(0.5);
}
