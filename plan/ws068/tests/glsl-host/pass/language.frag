#version 100
// GLSL ES 1.00 fragment shader features: precision per declaration, the derivatives extension, built-ins.
#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec4 v_color;
varying float v_fog[2];
uniform sampler2D u_texture;
uniform samplerCube u_cube;
uniform lowp vec4 u_fog_color;

highp float luminance(vec3 color)
{
	return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
	vec4 color = v_color;
	float fog = clamp(v_fog[0], 0.0, 1.0);
	vec2 coordinate = gl_FragCoord.xy / 256.0;

	color *= texture2D(u_texture, coordinate, 0.5);
	color += textureCube(u_cube, vec3(coordinate, 1.0)) * 0.1;
	color.rgb = mix(color.rgb, u_fog_color.rgb, fog);
	color.a *= step(0.1, luminance(color.rgb)) + fwidth(fog);
	if (!gl_FrontFacing)
		color = vec4(1.0) - color;
	if (all(lessThan(color.rgb, vec3(0.01))))
		discard;
	gl_FragData[0] = color + vec4(gl_PointCoord, 0.0, 0.0) * 0.0;
}
