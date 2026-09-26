#version 330 core
// GLSL 3.30: layout locations, an out block, gl_InstanceID, non-square matrices, floatBitsToUint.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
out VS_OUT {
	vec3 normal;
	flat int instance;
	noperspective float depth;
} vs_out;
uniform mat4 u_model;
uniform mat3x4 u_extra;

void main()
{
	vs_out.normal = mat3(u_model) * normal;
	vs_out.instance = gl_InstanceID;
	vs_out.depth = float(floatBitsToUint(position.z) & 255u);
	gl_Position = u_model * vec4(position, 1.0) + u_extra * vec3(0.0);
}
