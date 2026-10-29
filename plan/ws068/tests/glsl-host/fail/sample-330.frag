#version 330
// expect: error: 'gl_SampleID' is not declared
out vec4 colour;
void main()
{
	colour = vec4(float(gl_SampleID));
}
