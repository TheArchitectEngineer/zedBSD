// expect: 0:6: error: no overload of built-in 'texture2DLod' takes these arguments in this shader
precision mediump float;
uniform sampler2D s;
void main()
{
	gl_FragColor = texture2DLod(s, vec2(0.5), 0.0);
}
