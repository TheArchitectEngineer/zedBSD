// expect: 0:6: error: an array size must be a constant integer expression
uniform int n;
void main()
{
	float a[2];
	float b[n];
	gl_Position = vec4(a[0]);
}
