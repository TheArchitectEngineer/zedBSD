// expect: 0 255 0 255
// Matrices: constructors, products, columns, resizing, arithmetic.
// uniform: u_m 1 2 3 4 5 6 7 8 9
precision highp float;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

uniform mat3 u_m;

void main()
{
	int failed = 0;
	mat2 a = mat2(1.0, 2.0, 3.0, 4.0);
	mat2 b = mat2(2.0);
	mat3 c = mat3(mat4(2.0));
	mat4 d = mat4(u_m);
	mat2 e;

	if (!near(a * vec2(1.0, 1.0), vec2(4.0, 6.0))) failed = 1;
	if (!near(vec2(1.0, 1.0) * a, vec2(3.0, 7.0))) failed = 2;
	e = a * b;
	if (!near(e[1], vec2(6.0, 8.0))) failed = 3;
	e = a * a;
	if (!near(e[0], vec2(7.0, 10.0)) || !near(e[1], vec2(15.0, 22.0))) failed = 4;
	if (!near(c[2], vec3(0.0, 0.0, 2.0))) failed = 5;
	if (!near(d[3], vec4(0.0, 0.0, 0.0, 1.0)) || !near(d[1], vec4(4.0, 5.0, 6.0, 0.0))) failed = 6;
	e = a + b;
	if (!near(e[0], vec2(3.0, 2.0))) failed = 7;
	e = a * 2.0;
	if (!near(e[1], vec2(6.0, 8.0))) failed = 8;
	e = -a;
	if (!near(e[0][1], -2.0)) failed = 9;
	e = a;
	e[0] = vec2(9.0, 8.0);
	e[1][0] = 7.0;
	if (!near(e[0], vec2(9.0, 8.0)) || !near(e[1], vec2(7.0, 4.0))) failed = 10;
	if (!near(u_m[2].z, 9.0)) failed = 11;
	if (!near((u_m * vec3(1.0, 0.0, 0.0)), vec3(1.0, 2.0, 3.0))) failed = 12;
	if (a == b || a != mat2(1.0, 2.0, 3.0, 4.0)) failed = 13;
	e = a / 2.0 - b;
	if (!near(e[1], vec2(1.5, 0.0))) failed = 14;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
