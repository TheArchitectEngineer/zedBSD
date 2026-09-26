// expect: 0:3: error: no precision specified for float
// expect: 0:6: error: no precision specified for float
varying vec4 v;
void main()
{
	vec2 x = v.xy;
	gl_FragColor = v;
}
