#version 330
// expect: 0:8: error: an assignment cannot write 'a', which is read-only
uniform Block {
	vec4 a;
};
void main()
{
	a = vec4(1.0);
	gl_Position = a;
}
