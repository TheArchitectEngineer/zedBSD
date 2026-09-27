#version 150
// expect: EmitVertex
void main()
{
	EmitVertex();
	gl_Position = vec4(0.0);
}
