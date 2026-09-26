// expect: 0 255 0 255
// Texture lookups: the four texels of the 2x2 texture, a projective lookup, a bias.
precision mediump float;
uniform sampler2D u_texture;

vec4 fetch(sampler2D s, vec2 uv)
{
	return texture2D(s, uv);
}

void main()
{
	int failed = 0;

	if (texture2D(u_texture, vec2(0.25, 0.25)) != vec4(1.0, 0.0, 0.0, 1.0)) failed = 1;
	if (texture2D(u_texture, vec2(0.75, 0.25)) != vec4(0.0, 1.0, 0.0, 1.0)) failed = 2;
	if (texture2D(u_texture, vec2(0.25, 0.75)) != vec4(0.0, 0.0, 1.0, 1.0)) failed = 3;
	if (fetch(u_texture, vec2(0.75, 0.75)) != vec4(1.0)) failed = 4;
	if (texture2DProj(u_texture, vec3(1.5, 0.5, 2.0)) != vec4(0.0, 1.0, 0.0, 1.0)) failed = 5;
	if (texture2DProj(u_texture, vec4(0.5, 1.5, 0.0, 2.0)) != vec4(0.0, 0.0, 1.0, 1.0)) failed = 6;
	if (texture2D(u_texture, vec2(0.75, 0.75), 0.0) != vec4(1.0)) failed = 7;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
