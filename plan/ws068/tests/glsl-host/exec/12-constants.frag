// expect: 0 255 0 255
// Constant expressions: folded values, const variables, built-in constants.
precision highp float;
const float PI = 3.14159265;
const vec3 AXIS = vec3(0.0, 1.0, 0.0);
const int N = 2 * 3 + 1;
const float HALF_PI = PI / 2.0;
const mat2 SWAP = mat2(0.0, 1.0, 1.0, 0.0);

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

void main()
{
	int failed = 0;
	float a[N];
	if (!near(sin(HALF_PI), 1.0)) failed = 1;
	if (N != 7) failed = 2;
	if (!near(SWAP * vec2(1.0, 2.0), vec2(2.0, 1.0))) failed = 3;
	if (!near(AXIS.yx, vec2(1.0, 0.0))) failed = 4;
	if (gl_MaxDrawBuffers != 1 || gl_MaxVertexAttribs < 8) failed = 5;
	a[N - 1] = 1.0;
	if (a[6] != 1.0) failed = 6;
	if (!near(vec4(1.0, vec2(2.0, 3.0), 4.0), vec4(1.0, 2.0, 3.0, 4.0))) failed = 7;
	if (int(2.9) != 2 || int(-2.9) != -2 || float(3) != 3.0 || !bool(1.0) || bool(0)) failed = 8;
	if (ivec2(vec2(1.5, 2.5)) != ivec2(1, 2)) failed = 9;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
