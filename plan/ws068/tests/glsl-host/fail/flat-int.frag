#version 130
// expect: 0:3: error: integer inputs of a fragment shader must be flat
in int index;
out vec4 color;
void main()
{
	color = vec4(float(index));
}
