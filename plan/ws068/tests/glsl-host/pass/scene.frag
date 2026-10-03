// egltest's scene (ws068-p008) in GLSL ES 1.00: the colour, times the texture when u_textured is 1.
precision mediump float;
varying vec4 v_color;
varying vec2 v_uv;
uniform float u_textured;
uniform sampler2D u_texture;

void main()
{
	vec4 texel = texture2D(u_texture, v_uv);
	gl_FragColor = mix(v_color, v_color * texel, u_textured);
}
