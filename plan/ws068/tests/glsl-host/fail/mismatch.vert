// expect: 0:5: error: cannot assign vec3 to vec4
attribute vec4 a;
void main()
{
	gl_Position = a.xyz;
}
