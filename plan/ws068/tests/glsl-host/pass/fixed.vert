#version 100
// libGL's fixed-function vertex stage (userland/retro/libGL/shaders/fixed.vert) written in GLSL ES 1.00:
// the transforms, per-vertex lighting through a function-like macro, and the colour material.
#ifndef LIGHTS
#define LIGHTS 8
#endif

attribute vec4 a_position;
attribute vec4 a_color;
attribute vec3 a_normal;
attribute vec4 a_texcoord;
varying vec4 v_color;
varying vec4 v_texcoord;

uniform mat4 u_mvp;
uniform mat4 u_modelview;
uniform mat4 u_normal_matrix;
uniform mat4 u_texture_matrix;
uniform vec4 u_material[5];
uniform vec4 u_lights[40];
uniform vec4 u_flags;
uniform vec4 u_flags2;
uniform vec4 u_params;

#define LIGHT(n) \
	{ \
		vec4 position = u_lights[n * 5]; \
		vec4 attenuation = u_lights[n * 5 + 4]; \
		vec3 toward = position.xyz - eye.xyz * position.w; \
		float distance = length(toward); \
		vec3 direction = toward / max(distance, 1e-6); \
		float fall = mix(1.0, 1.0 / max(attenuation.x + attenuation.y * distance + attenuation.z * distance * distance, 1e-6), position.w); \
		float lambert = max(dot(normal, direction), 0.0); \
		vec3 half_vector = normalize(direction + vec3(0.0, 0.0, 1.0)); \
		float shine = pow(max(dot(normal, half_vector), 1e-6), u_params.x) * step(1e-6, lambert); \
		lit += attenuation.w * fall * (u_lights[n * 5 + 1] * ambient + lambert * u_lights[n * 5 + 2] * diffuse + \
			shine * u_lights[n * 5 + 3] * u_material[3]); \
	}

void main()
{
	vec4 eye = u_modelview * a_position;
	vec4 ambient = mix(u_material[1], a_color, u_flags.y);
	vec4 diffuse = mix(u_material[2], a_color, u_flags.z);
	vec3 normal = vec3(u_normal_matrix * vec4(a_normal, 0.0));
	vec4 lit;

	normal = mix(normal, normalize(normal), u_flags.w);
	lit = u_material[4] + u_material[0] * ambient;
	LIGHT(0)
#if LIGHTS > 1
	LIGHT(1)
#endif
#if LIGHTS > 2
	LIGHT(2)
	LIGHT(3)
#endif
#if LIGHTS > 4
	LIGHT(4)
	LIGHT(5)
	LIGHT(6)
	LIGHT(7)
#endif
	lit.a = diffuse.a;

	v_color = clamp(mix(a_color, lit, u_flags.x), 0.0, 1.0);
	v_texcoord = u_texture_matrix * a_texcoord;
	gl_Position = u_mvp * a_position;
}
