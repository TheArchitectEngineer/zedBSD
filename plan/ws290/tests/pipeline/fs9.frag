#version 450
// Nine sampled images read by the fragment stage, other than the vertex stage's eight.
layout(location = 0) in vec3 shade;
layout(location = 0) out vec4 color;
layout(set = 2, binding = 0) uniform sampler2D f0;
layout(set = 2, binding = 1) uniform sampler2D f1;
layout(set = 2, binding = 2) uniform sampler2D f2;
layout(set = 2, binding = 3) uniform sampler2D f3;
layout(set = 2, binding = 4) uniform sampler2D f4;
layout(set = 2, binding = 5) uniform sampler2D f5;
layout(set = 2, binding = 6) uniform sampler2D f6;
layout(set = 2, binding = 7) uniform sampler2D f7;
layout(set = 2, binding = 8) uniform sampler2D f8;
void main()
{
	vec2 uv = shade.xy;
	color = texture(f0, uv) + texture(f1, uv) + texture(f2, uv) + texture(f3, uv) + texture(f4, uv) +
		texture(f5, uv) + texture(f6, uv) + texture(f7, uv) + texture(f8, uv);
}
