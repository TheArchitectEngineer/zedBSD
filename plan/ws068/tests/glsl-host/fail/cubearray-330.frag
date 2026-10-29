#version 330
// expect: error:
uniform samplerCubeArray u_cubes;
out vec4 colour;
void main()
{
	colour = texture(u_cubes, vec4(1.0));
}
