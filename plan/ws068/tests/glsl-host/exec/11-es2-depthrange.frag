// expect: 0 255 0 255
// uniform: gl_DepthRange.near 0.25
// uniform: gl_DepthRange.far 0.75
// uniform: gl_DepthRange.diff 0.5
// ws068-p004: gl_DepthRange, the uniform libGLESv2 fills from glDepthRangef.
precision mediump float;

void main()
{
	int failed = 0;

	if (gl_DepthRange.near != 0.25) failed = 1;
	if (gl_DepthRange.far != 0.75) failed = 2;
	if (gl_DepthRange.diff != gl_DepthRange.far - gl_DepthRange.near) failed = 3;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
