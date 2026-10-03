#version 300 es
// expect: 0:3: error: in and out blocks need desktop GLSL 1.50
out Block {
	vec4 value;
} block;
void main()
{
	block.value = vec4(1.0);
	gl_Position = vec4(0.0);
}
