#version 450
// The Mali compiler's host check: textureGradOffset of a 2D, a 2D array and a 3D image, and of a 2D shadow sampler.
layout(set = 0, binding = 0) uniform sampler2D flat2d;
layout(set = 0, binding = 1) uniform sampler2DArray layers;
layout(set = 0, binding = 2) uniform sampler3D volume;
layout(set = 0, binding = 3) uniform sampler2DShadow shadow;
layout(location = 0) in vec4 v;
layout(location = 1) in vec4 dx;
layout(location = 2) in vec4 dy;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
void main()
{
	color = textureGradOffset(flat2d, v.xy, dx.xy, dy.xy, ivec2(1, -2)) + textureGradOffset(layers, vec3(v.zw, 1.0), dx.zw, dy.wz, ivec2(-3, 2));
	second = textureGradOffset(volume, v.xyz, dx.xyz, dy.yzw, ivec3(2, -1, 3));
	second.w = textureGradOffset(shadow, vec3(v.yx, v.z * 0.5), dx.yw, dy.xz, ivec2(-1, 1));
}
