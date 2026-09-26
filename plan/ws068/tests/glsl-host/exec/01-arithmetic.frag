// expect: 0 255 0 255
// Arithmetic, swizzles, compound assignment, increments, unary operators.
precision highp float;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

void main()
{
	int failed = 0;
	vec4 v = vec4(1.0, 2.0, 3.0, 4.0);
	vec3 w;
	float x = 2.0;
	int i = 7;
	int j;

	w = v.zyx;
	if (!near(w, vec3(3.0, 2.0, 1.0))) failed = 1;
	v.xy = v.yx;
	if (!near(v, vec4(2.0, 1.0, 3.0, 4.0))) failed = 2;
	v.w = 10.0;
	if (!near(v.w, 10.0)) failed = 3;
	x += 3.0;
	x *= 2.0;
	x -= 1.0;
	x /= 3.0;
	if (!near(x, 3.0)) failed = 4;
	j = i++;
	if (j != 7 || i != 8) failed = 5;
	j = --i;
	if (j != 7 || i != 7) failed = 6;
	if (i / 2 != 3) failed = 7;
	if (!near(-v.x, -2.0)) failed = 8;
	if (!near(v * 2.0, vec4(4.0, 2.0, 6.0, 20.0))) failed = 9;
	if (!near(2.0 + v.xy, vec2(4.0, 3.0))) failed = 10;
	if (!near(vec3(1.0) / vec3(2.0, 4.0, 8.0), vec3(0.5, 0.25, 0.125))) failed = 11;
	w.zx += vec2(1.0, 2.0);
	if (!near(w, vec3(5.0, 2.0, 2.0))) failed = 12;
	if (!(v.x > 1.0 && v.y <= 1.0 && !(v.z < 1.0))) failed = 13;
	if (i - 10 != -3) failed = 14;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
