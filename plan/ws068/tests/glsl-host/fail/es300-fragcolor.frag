#version 300 es
// expect: 0:6: error: 'gl_FragColor' is not declared
precision mediump float;
void main()
{
	gl_FragColor = vec4(1.0);
}
