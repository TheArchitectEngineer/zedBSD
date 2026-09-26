// expect: 0:4: error: too many arguments to the vec2 constructor
void main()
{
	vec2 v = vec2(1.0, 2.0, 3.0);
	gl_Position = vec4(v, 0.0, 1.0);
}
