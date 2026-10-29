#version 300 es
// expect: 0:4: error: layout(index) needs GLSL 3.30
precision mediump float;
layout(location = 0, index = 1) out vec4 colour;
void main()
{
	colour = vec4(1.0);
}
