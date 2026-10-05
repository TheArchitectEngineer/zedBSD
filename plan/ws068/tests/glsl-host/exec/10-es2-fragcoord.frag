// expect: 0 255 0 255
// uniform: gl_ZedFragment 10 0 0 1
// ws068-p004: gl_FragCoord.y is a + b * y with (a, b, c, d) the hidden uniform gl_ZedFragment; a 10, b 0 make it 10 everywhere, x stays.
precision mediump float;

void main()
{
	int failed = 0;

	if (gl_FragCoord.y != 10.0) failed = 1;
	if (gl_FragCoord.x <= 0.0 || gl_FragCoord.x >= 4.0) failed = 2;
	if (fract(gl_FragCoord.x) != 0.5) failed = 3;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
