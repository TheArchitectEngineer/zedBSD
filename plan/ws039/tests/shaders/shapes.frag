#version 450
// Samples of a 3D image, an array of 2D images, a cube and an array of cubes in a pixel program: with derivatives,
// a bias, at a level, with gradients and an offset (3D), texel fetches, depth compares of a cube, and size queries.
layout(location = 0) in vec3 direction;
layout(location = 1) in vec4 extra;
layout(set = 0, binding = 0) uniform sampler3D volume;
layout(set = 0, binding = 1) uniform sampler2DArray layers;
layout(set = 0, binding = 2) uniform samplerCube cube;
layout(set = 0, binding = 3) uniform samplerCubeArray cubes;
layout(set = 1, binding = 0) uniform samplerCubeShadow cube_depth;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 more;
layout(location = 2) out vec4 rest;
layout(location = 3) out vec4 sizes;
void main()
{
	vec3 at = direction * 0.25 + 0.5;
	float layer = extra.x * 1.5 + 1.0;
	vec4 a = texture(volume, at);
	vec4 b = textureLod(volume, at.zyx, 0.0);
	vec4 c = textureGradOffset(volume, at, extra.xyz * 0.125, extra.yzw * 0.0625, ivec3(1, -1, 1));
	vec4 d = texture(layers, vec3(at.xy, layer));
	vec4 e = textureGrad(layers, vec3(at.yx, layer + 0.5), extra.xy * 0.125, extra.zw * 0.125);
	vec4 f = texture(cube, direction);
	vec4 g = texture(cube, direction.zxy, 0.5);
	vec4 h = textureLod(cubes, vec4(direction, layer), 0.0);
	vec4 i = texture(cubes, vec4(direction.yzx, extra.w));
	float s = texture(cube_depth, vec4(direction, extra.y * 0.25 + 0.5));
	vec4 j = texelFetch(volume, ivec3(int(abs(extra.x) * 3.0) & 3, int(abs(extra.y) * 3.0) & 3, int(abs(extra.z)) & 1), 0);
	vec4 k = texelFetch(layers, ivec3(int(abs(extra.w) * 3.0) & 3, 1, int(abs(extra.x))), 0);
	ivec3 v = textureSize(volume, 0);
	ivec3 l = textureSize(layers, 0);
	ivec2 q = textureSize(cube, 0);
	ivec3 r = textureSize(cubes, 0);
	color = a + b * 0.5 + c * 0.25;
	more = d + e * 0.5 + j;
	rest = f + g * 0.5 + h * 0.25 + i * 0.125 + vec4(s) + k;
	sizes = vec4(float(v.x + v.y * 3 + v.z * 7), float(l.x + l.y * 3 + l.z * 7), float(q.x + q.y * 3),
		     float(r.x + r.y * 3 + r.z * 7));
}
