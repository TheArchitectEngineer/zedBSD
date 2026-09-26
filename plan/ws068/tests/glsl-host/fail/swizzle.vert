// expect: 0:6: error: swizzle 'xyzw' is outside the vec2
// expect: 0:7: error: an assignment cannot write swizzle 'xx' (a component repeats)
void main()
{
	vec2 v = vec2(1.0);
	vec4 w = v.xyzw;
	v.xx = vec2(2.0);
	gl_Position = w;
}
