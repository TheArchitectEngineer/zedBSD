// expect: 0 255 0 255
// Uniforms: scalars, vectors, matrices, arrays, a struct and an array of structs, bools and ints.
// uniform: u_scale 2.5
// uniform: u_offset 1 2 3 4
// uniform: u_matrix 1 2 3 4
// uniform: u_list 1 2 3
// uniform: u_light.colour 0.5 0.25 0.125
// uniform: u_light.power 8
// uniform: u_lights[1].colour 1 1 1
// uniform: u_lights[1].power 3
// uniform: u_flag 1
// uniform: u_count 5
// uniform: u_index 2
// uniform: u_mat3 1 0 0 0 2 0 0 0 3
precision highp float;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

struct Light {
	vec3 colour;
	float power;
};
uniform float u_scale;
uniform vec4 u_offset;
uniform mat2 u_matrix;
uniform float u_list[3];
uniform Light u_light;
uniform Light u_lights[2];
uniform bool u_flag;
uniform int u_count;
uniform int u_index;
uniform mat3 u_mat3;

void main()
{
	int failed = 0;
	Light copy;

	if (!near(u_scale, 2.5)) failed = 1;
	if (!near(u_offset, vec4(1.0, 2.0, 3.0, 4.0))) failed = 2;
	if (!near(u_matrix[1], vec2(3.0, 4.0))) failed = 3;
	if (!near(u_matrix * vec2(1.0, 1.0), vec2(4.0, 6.0))) failed = 4;
	if (!near(u_list[2], 3.0)) failed = 5;
	if (!near(u_list[u_index], 3.0)) failed = 6;
	if (!near(u_light.colour, vec3(0.5, 0.25, 0.125))) failed = 7;
	if (!near(u_lights[1].power, 3.0)) failed = 8;
	if (!u_flag) failed = 9;
	if (u_count != 5) failed = 10;
	copy = u_light;
	if (!near(copy.power * copy.colour.x, 4.0)) failed = 11;
	if (!near(u_mat3 * vec3(1.0), vec3(1.0, 2.0, 3.0))) failed = 12;
	if (!near(u_lights[0].power, 0.0)) failed = 13;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
