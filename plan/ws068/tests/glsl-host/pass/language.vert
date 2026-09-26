#version 100
// The language of GLSL ES 1.00 in one vertex shader: structs, arrays, functions with out and inout
// parameters, overloads, prototypes, loops, constant expressions, swizzles and the preprocessor.
#define SQUARE(x) ((x) * (x))
#define JOIN(a, b) a ## b
#define COUNT 4
#if defined(GL_ES) && __VERSION__ == 100 && (COUNT * 2 > 7)
#define GOOD 1
#else
#error the preprocessor is wrong
#endif
#ifdef UNDEFINED_NAME
#error not defined
#elif GOOD
precision highp float;
#endif
#undef GOOD
#line 100

struct Light {
	vec3 direction;
	vec4 colour;
	float strength[2];
};

struct Material {
	Light light;
	float shininess;
};

attribute vec4 a_position;
attribute vec3 a_normal;
varying vec4 v_color;
varying float v_fog[2];
uniform Material u_material;
uniform Light u_lights[2];
uniform bool u_enabled;
uniform ivec2 u_counts;

const float PI = 3.14159265;
const vec3 UP = vec3(0.0, 1.0, 0.0);
const int SIZE = COUNT + 1;
const float HALF = SQUARE(0.5) * 2.0;

float weights[SIZE];

float shade(vec3 normal, Light light);

float shade(vec3 normal, Light light)
{
	float lambert = max(dot(normal, -light.direction), 0.0);
	if (lambert <= 0.0)
		return 0.0;
	return lambert * light.strength[0] + light.strength[1];
}

vec4 shade(vec3 normal, Light light, float scale)
{
	return light.colour * shade(normal, light) * scale;
}

void split(in vec4 value, out vec2 low, out vec2 high)
{
	low = value.xy;
	high = value.zw;
}

void accumulate(inout vec4 total, vec4 value)
{
	total += value;
	total.w = 1.0;
}

void main()
{
	vec4 total = vec4(0.0);
	vec2 low;
	vec2 high;
	float JOIN(fo, g) = 0.0;
	int i;
	mat3 basis = mat3(1.0);
	bvec2 flags = bvec2(u_enabled, false);

	for (i = 0; i < SIZE; i++)
		weights[i] = float(i) / float(SIZE);
	for (int k = 0; k < 2; ++k) {
		if (!u_enabled)
			continue;
		accumulate(total, shade(normalize(a_normal), u_lights[k], weights[k + 1]));
		if (total.x > 10.0)
			break;
	}
	split(a_position, low, high);
	fog = length(low - high) * HALF;
	while (fog > PI)
		fog -= PI;
	do {
		fog *= 0.5;
	} while (fog > 1.0);
	total.rgb += shade(a_normal, u_material.light, u_material.shininess).rgb * UP.y;
	total.xy = total.yx;
	basis[1] = UP;
	total.xyz = basis * total.xyz;
	v_color = any(flags) ? total : vec4(float(u_counts.x), float(u_counts[1]), 0.0, 1.0);
	v_fog[0] = fog;
	v_fog[1] = -fog;
	gl_Position = a_position;
	gl_PointSize = 1.0;
}
