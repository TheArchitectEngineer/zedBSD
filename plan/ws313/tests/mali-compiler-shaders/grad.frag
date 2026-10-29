#version 450
// The Mali compiler's host check: textureGrad of a 2D, a 2D array and a 3D image, and of 2D and 2D array shadow samplers.
layout(set = 0, binding = 0) uniform sampler2D flat2d;
layout(set = 0, binding = 1) uniform sampler2DArray layers;
layout(set = 0, binding = 2) uniform sampler3D volume;
layout(set = 0, binding = 3) uniform sampler2DShadow shadow;
layout(set = 0, binding = 4) uniform sampler2DArrayShadow shadows;
layout(location = 0) in vec4 v;
layout(location = 1) in vec4 dx;
layout(location = 2) in vec4 dy;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
void main()
{
	color = textureGrad(flat2d, v.xy, dx.xy, dy.xy) + textureGrad(layers, vec3(v.zw, 2.0), dx.zw, dy.wz);
	second = textureGrad(volume, v.xyz, dx.xyz, dy.yzw);
	second.w = textureGrad(shadow, vec3(v.yx, v.z * 0.5), dx.yw, dy.xz) + textureGrad(shadows, vec4(v.xy, 1.0, v.w * 0.5), dx.xw, dy.zy);
}
