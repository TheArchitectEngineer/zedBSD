#version 450
// Eight sampled images read by the vertex stage.
layout(location = 0) in vec2 position;
layout(location = 0) out vec3 shade;
layout(set = 1, binding = 0) uniform sampler2D u0;
layout(set = 1, binding = 1) uniform sampler2D u1;
layout(set = 1, binding = 2) uniform sampler2D u2;
layout(set = 1, binding = 3) uniform sampler2D u3;
layout(set = 1, binding = 4) uniform sampler2D u4;
layout(set = 1, binding = 5) uniform sampler2D u5;
layout(set = 1, binding = 6) uniform sampler2D u6;
layout(set = 1, binding = 7) uniform sampler2D u7;
void main()
{
	vec4 sum = textureLod(u0, position, 0.0) + textureLod(u1, position, 0.0) + textureLod(u2, position, 0.0) +
		textureLod(u3, position, 0.0) + textureLod(u4, position, 0.0) + textureLod(u5, position, 0.0) +
		textureLod(u6, position, 0.0) + textureLod(u7, position, 0.0);
	gl_Position = vec4(position, 0.0, 1.0);
	shade = sum.xyz;
}
