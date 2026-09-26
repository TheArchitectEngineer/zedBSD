#version 300 es
// expect: 0:3: error: 'attribute' is a reserved word
attribute vec4 a;
void main()
{
	gl_Position = a;
}
