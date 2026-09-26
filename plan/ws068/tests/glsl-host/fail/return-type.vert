// expect: 0:4: error: cannot return vec2 from a function returning float
float f()
{
	return vec2(1.0);
}
void main()
{
	gl_Position = vec4(f());
}
