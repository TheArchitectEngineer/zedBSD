#version 100
// libGL's fixed-function fragment stage in GLSL ES 1.00: the colour times the texture, and the alpha test.
precision mediump float;
varying vec4 v_color;
varying vec4 v_texcoord;
uniform vec4 u_flags2;
uniform sampler2D u_texture;

void main()
{
	vec4 texel = texture2DProj(u_texture, v_texcoord);
	vec4 color = mix(v_color, mix(v_color * texel, texel, u_flags2.y), u_flags2.x);
	float a = color.a;
	float ref = u_flags2.w;
	float f = u_flags2.z;
	bool fail;

	fail = (f == 1.0) || (f == 2.0 && !(a < ref)) || (f == 3.0 && !(a == ref)) || (f == 4.0 && !(a <= ref)) ||
	       (f == 5.0 && !(a > ref)) || (f == 6.0 && !(a != ref)) || (f == 7.0 && !(a >= ref));
	if (fail)
		discard;
	gl_FragColor = color;
}
