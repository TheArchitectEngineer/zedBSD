// expect: 0:6: error: no overload of built-in 'dFdx' takes these arguments in this shader
precision mediump float;
varying float v;
void main()
{
	gl_FragColor = vec4(dFdx(v));
}
