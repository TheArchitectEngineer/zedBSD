#version 450
// vkcompile: twelve texel fetches, each at a texel the one before chose, their bytes summed (the waits between a
// fetch and the use of its result).
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) out vec4 color;
void main()
{
	uvec2 c = uvec2(gl_FragCoord.xy);
	ivec2 at = ivec2(c & 15U);
	uvec4 sum = uvec4(0U);
	for (int i = 0; i < 12; i++) {
		uvec4 t = uvec4(texelFetch(picture, at, 0) * 255.0 + 0.5);
		sum += t;
		at = ivec2((t.xy ^ uvec2(uint(i), uint(i) * 3U)) & 15U);
	}
	color = vec4(sum & 255U) / 255.0;
}
