// discard leaves the target's clear colour.
// uniform: u_zero 0
// expect: 0 0 0 0
precision mediump float;
uniform float u_zero;
void main()
{
	if (u_zero < 1.0)
		discard;
	gl_FragColor = vec4(1.0);
}
