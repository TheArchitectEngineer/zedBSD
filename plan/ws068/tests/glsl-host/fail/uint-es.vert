// expect: 0:4: error: unsigned integer constants need GLSL 1.30
void main()
{
	int x = int(3u);
	gl_Position = vec4(float(x));
}
