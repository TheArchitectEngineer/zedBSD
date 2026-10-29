#version 400
// expect: 0:3: error: patch qualifies a tessellation control shader's outputs and an evaluation shader's inputs
patch out vec4 colour;
void main()
{
	gl_Position = vec4(0.0);
}
