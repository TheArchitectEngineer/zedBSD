// expect: 0:4: error: names starting with 'gl_' are reserved ('gl_mine')
void main()
{
	float gl_mine = 1.0;
	gl_Position = vec4(gl_mine);
}
