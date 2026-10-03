// expect: error: function 'f' is recursive
float f(float x);
float g(float x) { return f(x); }
float f(float x) { return g(x - 1.0); }
void main()
{
	gl_Position = vec4(f(1.0));
}
