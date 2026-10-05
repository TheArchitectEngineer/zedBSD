// ws068-p004: gl_FragCoord and gl_PointCoord (turned into GL's directions through gl_ZedFragment), gl_DepthRange in both stages, an array of samplers.
precision mediump float;
uniform sampler2D u_textures[3];
varying float v_depth;

void main()
{
	vec4 colour = texture2D(u_textures[0], gl_PointCoord) + texture2D(u_textures[2], gl_FragCoord.xy / 64.0);
	gl_FragColor = colour * gl_DepthRange.far + vec4(v_depth);
}
