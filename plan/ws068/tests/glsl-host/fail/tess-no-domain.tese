#version 400
// expect: error: a tessellation evaluation shader needs its domain
void main()
{
	gl_Position = vec4(gl_TessCoord, 1.0);
}
