// expect: 0:5: error: 'missing' is not declared
attribute vec4 a;
void main()
{
	gl_Position = a * missing;
}
