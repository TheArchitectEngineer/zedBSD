#version 330
// expect: error: no overload of built-in 'textureGather' takes these arguments in this shader
uniform sampler2D u_texture;
out vec4 colour;
void main()
{
	colour = textureGather(u_texture, vec2(0.5));
}
