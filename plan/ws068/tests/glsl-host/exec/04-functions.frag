// expect: 0 255 0 255
// Functions: in, out and inout parameters, overloads, early returns (also from loops), nested calls.
// uniform: u_value 3
precision highp float;
uniform float u_value;

float square(float x) { return x * x; }
vec2 square(vec2 x) { return x * x; }

void split(vec4 v, out vec2 low, out vec2 high)
{
	low = v.xy;
	high = v.zw;
}

void bump(inout float x, float by)
{
	x += by;
}

float sign_of(float x)
{
	if (x > 0.0)
		return 1.0;
	if (x < 0.0)
		return -1.0;
	return 0.0;
}

int find(float target)
{
	for (int i = 0; i < 10; i++) {
		if (float(i) == target)
			return i;
	}
	return -1;
}

int find_nested(float target)
{
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (float(i * 4 + j) == target)
				return i * 10 + j;
		}
	}
	return -1;
}

float twice(float x) { return square(x) + square(x); }

void main()
{
	int failed = 0;
	vec2 low;
	vec2 high;
	float x = 1.0;
	vec4 v = vec4(1.0, 2.0, 3.0, 4.0);

	if (square(u_value) != 9.0) failed = 1;
	if (square(vec2(2.0, 3.0)) != vec2(4.0, 9.0)) failed = 2;
	split(v, low, high);
	if (low != vec2(1.0, 2.0) || high != vec2(3.0, 4.0)) failed = 3;
	bump(x, 2.0);
	bump(x, u_value);
	if (x != 6.0) failed = 4;
	if (sign_of(-u_value) != -1.0 || sign_of(u_value) != 1.0 || sign_of(0.0) != 0.0) failed = 5;
	if (find(u_value) != 3) failed = 6;
	if (find(20.0) != -1) failed = 7;
	if (find_nested(u_value + 3.0) != 12) failed = 8;
	if (twice(u_value) != 18.0) failed = 9;
	split(vec4(square(2.0), 0.0, 0.0, 1.0), v.zw, v.xy);
	if (v != vec4(0.0, 1.0, 4.0, 0.0)) failed = 10;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
