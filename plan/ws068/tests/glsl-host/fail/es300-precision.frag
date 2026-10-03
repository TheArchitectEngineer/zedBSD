#version 300 es
// expect: 0:3: error: no precision specified for float
out vec4 color;
void main()
{
	color = vec4(1.0);
}
