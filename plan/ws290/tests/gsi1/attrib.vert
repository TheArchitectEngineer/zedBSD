#version 450
// Attributes of different sizes, packed into registers in location order.
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 tint;
layout(location = 0) out vec4 a;
layout(location = 1) out vec4 b;
void main()
{
	gl_Position = position;
	a = vec4(uv, normal.zx) + tint;
	b = vec4(normal, tint.w) * position.w;
}
