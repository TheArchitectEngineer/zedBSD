#version 400
// GLSL 4.00's texture gathers of 2D, 2D array and cube samplers of each texel type.
uniform sampler2D u_plain;
uniform isampler2DArray u_layers;
uniform usamplerCube u_cube;
in vec3 v_coordinate;
out vec4 colour;
void main()
{
	vec4 red = textureGather(u_plain, v_coordinate.xy);
	ivec4 layer = textureGather(u_layers, v_coordinate);
	uvec4 face = textureGather(u_cube, v_coordinate);
	vec4 near = textureGatherOffset(u_plain, v_coordinate.xy, ivec2(1, -1));
	ivec4 shifted = textureGatherOffset(u_layers, v_coordinate, ivec2(0, 1));
	colour = red + near + vec4(layer + shifted) + vec4(face);
}
