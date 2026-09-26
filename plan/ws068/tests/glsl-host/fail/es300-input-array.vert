#version 300 es
// expect: 0:3: error: vertex shader inputs cannot be arrays
in vec4 a[2];
void main()
{
	gl_Position = a[0];
}
