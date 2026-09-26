// expect: 0:4: error: cannot initialize float with int
void main()
{
	float x = 1;
	gl_Position = vec4(x);
}
