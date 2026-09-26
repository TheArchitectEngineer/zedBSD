#version 300 es
// expect: 0:8: error: no overload of built-in 'texture2D' takes these arguments in this shader
precision mediump float;
uniform sampler2D s;
out vec4 color;
void main()
{
	color = texture2D(s, vec2(0.5));
}
