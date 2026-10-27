#version 450
// Seventeen sampled images: one texture slot more than a pipeline has.
layout(set = 0, binding = 0) uniform sampler2D t0;
layout(set = 0, binding = 1) uniform sampler2D t1;
layout(set = 0, binding = 2) uniform sampler2D t2;
layout(set = 0, binding = 3) uniform sampler2D t3;
layout(set = 0, binding = 4) uniform sampler2D t4;
layout(set = 0, binding = 5) uniform sampler2D t5;
layout(set = 0, binding = 6) uniform sampler2D t6;
layout(set = 0, binding = 7) uniform sampler2D t7;
layout(set = 0, binding = 8) uniform sampler2D t8;
layout(set = 0, binding = 9) uniform sampler2D t9;
layout(set = 0, binding = 10) uniform sampler2D t10;
layout(set = 0, binding = 11) uniform sampler2D t11;
layout(set = 0, binding = 12) uniform sampler2D t12;
layout(set = 0, binding = 13) uniform sampler2D t13;
layout(set = 0, binding = 14) uniform sampler2D t14;
layout(set = 0, binding = 15) uniform sampler2D t15;
layout(set = 0, binding = 16) uniform sampler2D t16;
layout(location = 0) in vec4 uv;
layout(location = 0) out vec4 result;
void main()
{
	result = texture(t0, uv.xy) + texture(t1, uv.xy) + texture(t2, uv.xy) + texture(t3, uv.xy) + texture(t4, uv.xy) + texture(t5, uv.xy) + texture(t6, uv.xy) + texture(t7, uv.xy) + texture(t8, uv.xy) + texture(t9, uv.xy) + texture(t10, uv.xy) + texture(t11, uv.xy) + texture(t12, uv.xy) + texture(t13, uv.xy) + texture(t14, uv.xy) + texture(t15, uv.xy) + texture(t16, uv.xy);
}
