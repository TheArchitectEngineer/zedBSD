// expect: 0:4: error: no overload of built-in 'dot' takes these arguments
void main()
{
	float d = dot(vec2(1.0), vec3(1.0));
	gl_Position = vec4(d);
}
