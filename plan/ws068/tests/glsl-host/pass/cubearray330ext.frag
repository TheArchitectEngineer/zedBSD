#version 330 core
#extension GL_ARB_texture_cube_map_array : require
// GLSL 3.30 with GL_ARB_texture_cube_map_array: the cube map array samplers' lookups, sizes, gradients and gathers.
uniform samplerCubeArray u_cubes;
uniform isamplerCubeArray u_integers;
uniform samplerCubeArrayShadow u_depths;
in vec4 v_direction;
out vec4 colour;
void main()
{
	vec4 plain = texture(u_cubes, v_direction);
	vec4 biased = texture(u_cubes, v_direction, 0.5);
	vec4 level = textureLod(u_cubes, v_direction, 1.0);
	vec4 graded = textureGrad(u_cubes, v_direction, vec3(0.1), vec3(0.1));
	ivec4 integer = texture(u_integers, v_direction);
	float shadow = texture(u_depths, v_direction, 0.5);
	ivec3 size = textureSize(u_cubes, 0) + textureSize(u_depths, 0);
	colour = plain + biased + level + graded + vec4(integer) + vec4(shadow) + vec4(vec3(size), 1.0);
}
