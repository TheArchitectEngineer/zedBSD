// expect: 0 255 0 255
// Values computed by the vertex shader (a uniform, a function, an array varying) reach the fragment shader.
// uniform: u_base 0.25
precision highp float;
varying vec4 v_value;
varying float v_list[2];
varying mat2 v_matrix;

bool near(float a, float b) { return abs(a - b) < 0.001; }
bool near(vec2 a, vec2 b) { return all(lessThan(abs(a - b), vec2(0.001))); }
bool near(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.001))); }
bool near(vec4 a, vec4 b) { return all(lessThan(abs(a - b), vec4(0.001))); }

void main()
{
	int failed = 0;
	if (!near(v_value, vec4(0.25, 0.5, 0.75, 1.0))) failed = 1;
	if (!near(v_list[0], 2.0) || !near(v_list[1], 3.0)) failed = 2;
	if (!near(v_matrix[1], vec2(3.0, 4.0))) failed = 3;

	if (failed == 0)
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
