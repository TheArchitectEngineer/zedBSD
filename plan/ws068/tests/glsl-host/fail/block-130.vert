#version 130
// expect: 0:3: error: uniform blocks need GLSL 1.40 or OpenGL ES 3.00
uniform Block {
	vec4 a;
};
void main()
{
	gl_Position = a;
}
