#version 450
// The Mali compiler's host check: gathers, a compare gather, 2D and array shadow samplers, and a 2D array.
layout(set = 0, binding = 0) uniform sampler2D image;
layout(set = 0, binding = 1) uniform sampler2DShadow depth;
layout(set = 0, binding = 2) uniform sampler2DArrayShadow depths;
layout(set = 0, binding = 3) uniform sampler2DArray layers;
layout(location = 0) in vec4 v;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
layout(location = 2) out vec4 third;
void main()
{
	color = textureGather(image, v.xy, 2) + textureGatherOffset(image, v.zw, ivec2(1, -1), 1);
	second = textureGather(depth, v.yx, v.z);
	third = vec4(texture(depth, vec3(v.xy, v.w)), textureLod(depth, vec3(v.zw, v.x), 0.0),
		     texture(depths, vec4(v.xy, 1.0, v.z)), texture(layers, vec3(v.wz, 2.3)).y);
}
