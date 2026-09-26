#version 330
// expect: 0:4: error: samplers cannot be block members
uniform Block {
	sampler2D s;
};
out vec4 color;
void main()
{
	color = vec4(1.0);
}
