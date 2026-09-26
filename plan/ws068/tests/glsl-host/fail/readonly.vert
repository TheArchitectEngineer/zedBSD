// expect: 0:7: error: an assignment cannot write 'u', which is read-only
// expect: 0:8: error: an assignment cannot write 'K', which is read-only
uniform float u;
const float K = 1.0;
void main()
{
	u = 1.0;
	K = 2.0;
	gl_Position = vec4(u);
}
