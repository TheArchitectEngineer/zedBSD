// expect: 0 255 0 255
// ws068-p004: an array of samplers is one binding with a descriptor an element (both read the 2x2 texture here).
precision mediump float;
uniform sampler2D u_textures[2];
uniform sampler2D u_single;

void main()
{
	int failed = 0;

	if (texture2D(u_textures[0], vec2(0.25, 0.25)) != vec4(1.0, 0.0, 0.0, 1.0)) failed = 1;
	if (texture2D(u_textures[1], vec2(0.75, 0.25)) != vec4(0.0, 1.0, 0.0, 1.0)) failed = 2;
	if (texture2D(u_single, vec2(0.75, 0.75)) != vec4(1.0)) failed = 3;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
