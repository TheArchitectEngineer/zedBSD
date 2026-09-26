// expect: 0:6: error: no overload of 'f' takes these arguments
float f(float x) { return x; }
float f(vec2 x) { return x.x; }
void main()
{
	float y = f(vec3(1.0));
	gl_Position = vec4(y);
}
