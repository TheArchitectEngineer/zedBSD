// expect: 0 255 0 255
// version: 130
// GLSL 1.30: in and out, a flat int, a noperspective float, gl_VertexID, non-square matrices, array constructors.
#version 130
flat in int v_index;
noperspective in float v_value;
in vec3 v_product;
out vec4 color;

void main()
{
	int failed = 0;
	float list[3] = float[3](1.0, 2.0, 3.0);
	float other[] = float[](4.0, 5.0);

	if (v_index != 5) failed = 1;
	if (abs(v_value - 0.5) > 0.001) failed = 2;
	if (any(greaterThan(abs(v_product - vec3(1.0, 2.0, 3.0)), vec3(0.001)))) failed = 3;
	if (list.length() != 3 || other.length() != 2 || list[1] + other[1] != 7.0) failed = 4;
	if (list == float[3](1.0, 2.0, 4.0)) failed = 5;

	if (failed == 0)
		color = vec4(0.0, 1.0, 0.0, 1.0);
	else
		color = vec4(1.0, 0.0, float(failed) / 255.0, 1.0);
}
