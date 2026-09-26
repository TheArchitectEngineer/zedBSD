attribute vec2 a_position;
uniform float u_base;
varying vec4 v_value;
varying float v_list[2];
varying mat2 v_matrix;

vec4 ramp(float base)
{
	return vec4(base, base * 2.0, base * 3.0, base * 4.0);
}

void main()
{
	v_value = ramp(u_base);
	v_list[0] = 2.0;
	v_list[1] = 3.0;
	v_matrix = mat2(1.0, 2.0, 3.0, 4.0);
	gl_Position = vec4(a_position, 0.0, 1.0);
}
