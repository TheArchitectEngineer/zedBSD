#version 130
in vec2 a_position;
flat out int v_index;
noperspective out float v_value;
out vec3 v_product;
uniform mat2x3 u_unused;

void main()
{
	mat2x3 m = mat2x3(1.0, 2.0, 3.0, 0.0, 0.0, 0.0);
	v_index = 5 + gl_VertexID * 0;
	v_value = 0.5;
	v_product = m * vec2(1.0, 0.0);
	gl_Position = vec4(a_position, 0.0, 1.0);
}
