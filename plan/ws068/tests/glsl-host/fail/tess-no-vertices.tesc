#version 400
// expect: error: a tessellation control shader needs its output vertices
void main()
{
	gl_TessLevelOuter[0] = 1.0;
}
