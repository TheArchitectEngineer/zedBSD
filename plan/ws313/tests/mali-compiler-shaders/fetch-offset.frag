#version 450
// The Mali compiler's host check: texelFetchOffset of a 2D, a 2D array and a 3D image at a level the shader computes.
layout(set = 0, binding = 0) uniform sampler2D flat2d;
layout(set = 0, binding = 1) uniform sampler2DArray layers;
layout(set = 0, binding = 2) uniform sampler3D volume;
layout(location = 0) flat in ivec4 v;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
void main()
{
	color = texelFetchOffset(flat2d, v.xy, v.z & 3, ivec2(1, -2)) + texelFetchOffset(layers, ivec3(v.zw, 1), v.x & 1, ivec2(-3, 2));
	second = texelFetchOffset(volume, v.xyz, v.w & 1, ivec3(2, -1, 3)) + texelFetchOffset(flat2d, v.yx, 0, ivec2(-1, 1));
}
