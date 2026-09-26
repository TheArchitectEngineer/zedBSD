// expect: 0 255 0 255
// version: 330
#version 330 core
// Uniform blocks (the buffer's float at byte offset o is o / 4): std140 offsets of scalars, vectors, a matrix,
// an array, a row-major matrix, an array of structs, a bool and an int; a block with an instance name.
struct Item {
	vec4 a;
	float b;
};
layout(std140) uniform Lights {
	float first;
	vec3 direction;
	mat4 transform;
	float values[3];
	vec2 pair;
	layout(row_major) mat2 rows;
	Item items[2];
	bool flag;
	int count;
};
uniform Material {
	vec4 colour;
} material;
flat in float v_from_vertex;
layout(location = 0) out vec4 color;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

void main()
{
	int failed = 0;
	int i = int(first) + 1;

	if (!near(first, 0.0)) failed = 1;
	if (!near(direction, vec3(4.0, 5.0, 6.0))) failed = 2;
	if (!near(transform[0], vec4(8.0, 9.0, 10.0, 11.0)) || !near(transform[3], vec4(20.0, 21.0, 22.0, 23.0))) failed = 3;
	if (!near(values[0], 24.0) || !near(values[i], 28.0) || !near(values[2], 32.0)) failed = 4;
	if (!near(pair, vec2(36.0, 37.0))) failed = 5;
	if (!near(rows[0], vec2(40.0, 44.0)) || !near(rows[1], vec2(41.0, 45.0))) failed = 6;
	if (!near(items[1].a, vec4(56.0, 57.0, 58.0, 59.0)) || !near(items[0].b, 52.0) || !near(items[i].b, 60.0)) failed = 7;
	if (!flag) failed = 8;
	if (count != floatBitsToInt(65.0)) failed = 9;
	if (!near(material.colour, vec4(0.0, 1.0, 2.0, 3.0))) failed = 10;
	if (!near(v_from_vertex, 9.0)) failed = 11;
	if (!near((transform * vec4(1.0, 0.0, 0.0, 0.0)).y, 9.0)) failed = 12;
	if (failed == 0)
		color = vec4(0.0, 1.0, 0.0, 1.0);
	else
		color = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
