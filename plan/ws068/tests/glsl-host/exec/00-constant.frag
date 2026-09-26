// The simplest shader: a constant colour.
// expect: 255 128 64 255
precision mediump float;
void main()
{
	gl_FragColor = vec4(1.0, 0.5, 0.25, 1.0);
}
