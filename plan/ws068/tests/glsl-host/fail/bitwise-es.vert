// expect: 0:5: error: '%' and the bitwise operators need GLSL 1.30
void main()
{
	int a = 5;
	int b = a % 2;
	gl_Position = vec4(float(b));
}
