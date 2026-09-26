// expect: 0:5: error: a condition must be a bool, not float
void main()
{
	float x = 1.0;
	if (x)
		x = 2.0;
	gl_Position = vec4(x);
}
