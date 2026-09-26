// expect: 0:3: error: attributes exist only in vertex shaders
precision mediump float;
attribute vec4 a;
void main()
{
	gl_FragColor = a;
}
