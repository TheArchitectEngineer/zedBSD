#version 450
// Samples of a 3D image, an array of 2D images and a cube at level 0 in a vertex program, a gather, and a size query.
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 extra;
layout(set = 0, binding = 0) uniform sampler3D volume;
layout(set = 0, binding = 1) uniform sampler2DArray layers;
layout(set = 0, binding = 2) uniform samplerCube cube;
layout(location = 0) out vec4 value;
layout(location = 1) out vec4 more;
void main()
{
	vec4 a = textureLod(volume, extra.xyz * 0.25 + 0.5, 0.0);
	vec4 b = textureLod(layers, vec3(extra.xy, extra.z * 2.0), 0.0);
	vec4 c = textureLod(cube, extra.xyz + vec3(0.0, 0.0, 0.25), 0.0);
	vec4 d = textureGather(layers, vec3(extra.yx, extra.w));
	ivec3 s = textureSize(volume, 0);
	gl_Position = position;
	value = a + b * 0.5;
	more = c + d * 0.5 + vec4(float(s.x + s.y * 3 + s.z * 7));
}
