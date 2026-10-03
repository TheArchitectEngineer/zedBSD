// expect: 0 255 0 255
// Built-in functions of GLSL ES 1.00.
precision highp float;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

void main()
{
	int failed = 0;
	vec3 n = normalize(vec3(0.0, 3.0, 4.0));

	if (!near(radians(180.0), 3.14159265)) failed = 1;
	if (!near(degrees(3.14159265), 180.0)) failed = 2;
	if (!near(sin(0.5), 0.479426)) failed = 3;
	if (!near(cos(0.5), 0.877583)) failed = 4;
	if (!near(tan(0.5), 0.546302)) failed = 5;
	if (!near(asin(0.5), 0.523599)) failed = 6;
	if (!near(acos(0.5), 1.047198)) failed = 7;
	if (!near(atan(1.0, 2.0), 0.463648)) failed = 8;
	if (!near(atan(0.5), 0.463648)) failed = 9;
	if (!near(pow(2.0, 3.0), 8.0)) failed = 10;
	if (!near(exp(1.0), 2.718282)) failed = 11;
	if (!near(log(2.718282), 1.0)) failed = 12;
	if (!near(exp2(3.0), 8.0)) failed = 13;
	if (!near(log2(8.0), 3.0)) failed = 14;
	if (!near(sqrt(16.0), 4.0)) failed = 15;
	if (!near(inversesqrt(4.0), 0.5)) failed = 16;
	if (!near(abs(vec2(-1.5, 2.0)), vec2(1.5, 2.0))) failed = 17;
	if (!near(sign(vec3(-2.0, 0.0, 3.0)), vec3(-1.0, 0.0, 1.0))) failed = 18;
	if (!near(floor(-1.5), -2.0) || !near(ceil(1.2), 2.0) || !near(fract(1.25), 0.25)) failed = 19;
	if (!near(mod(7.0, 3.0), 1.0) || !near(mod(vec2(7.0, -1.0), 3.0), vec2(1.0, 2.0))) failed = 20;
	if (!near(min(vec2(1.0, 5.0), 3.0), vec2(1.0, 3.0)) || !near(max(2.0, 4.0), 4.0)) failed = 21;
	if (!near(clamp(vec3(-1.0, 0.5, 2.0), 0.0, 1.0), vec3(0.0, 0.5, 1.0))) failed = 22;
	if (!near(mix(vec2(0.0, 10.0), vec2(10.0, 20.0), 0.25), vec2(2.5, 12.5))) failed = 23;
	if (!near(step(0.5, vec2(0.2, 0.7)), vec2(0.0, 1.0))) failed = 24;
	if (!near(smoothstep(0.0, 1.0, 0.5), 0.5) || !near(smoothstep(0.0, 2.0, 0.5), 0.15625)) failed = 25;
	if (!near(length(vec3(0.0, 3.0, 4.0)), 5.0) || !near(distance(vec2(1.0, 1.0), vec2(4.0, 5.0)), 5.0)) failed = 26;
	if (!near(dot(vec3(1.0, 2.0, 3.0), vec3(4.0, 5.0, 6.0)), 32.0) || !near(dot(2.0, 3.0), 6.0)) failed = 27;
	if (!near(cross(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0)), vec3(0.0, 0.0, 1.0))) failed = 28;
	if (!near(n, vec3(0.0, 0.6, 0.8))) failed = 29;
	if (!near(reflect(vec2(1.0, -1.0), vec2(0.0, 1.0)), vec2(1.0, 1.0))) failed = 30;
	if (!near(faceforward(vec2(0.0, 1.0), vec2(0.0, 1.0), vec2(0.0, 1.0)), vec2(0.0, -1.0))) failed = 31;
	if (!near(faceforward(vec2(0.0, 1.0), vec2(0.0, -1.0), vec2(0.0, 1.0)), vec2(0.0, 1.0))) failed = 32;
	if (!near(refract(vec2(0.0, -1.0), vec2(0.0, 1.0), 1.0), vec2(0.0, -1.0))) failed = 33;
	if (!all(equal(lessThan(vec3(1.0, 2.0, 3.0), vec3(2.0)), bvec3(true, false, false)))) failed = 34;
	if (!any(greaterThanEqual(ivec2(1, 5), ivec2(5, 5))) || any(notEqual(vec2(1.0), vec2(1.0)))) failed = 35;
	if (not(bvec2(true, false)) != bvec2(false, true)) failed = 36;
	if (!near(matrixCompMult(mat2(1.0, 2.0, 3.0, 4.0), mat2(2.0))[0], vec2(2.0, 0.0))) failed = 37;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
