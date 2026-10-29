#version 450
// The Mali compiler's host check: cube and cube array sampling, and a cube shadow sampler's compare.
layout(set = 0, binding = 0) uniform samplerCube sky;
layout(set = 0, binding = 1) uniform samplerCubeArray skies;
layout(set = 0, binding = 2) uniform samplerCubeShadow shadow;
layout(push_constant) uniform P { vec4 d; vec4 e; vec4 f; } p;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
void main()
{
	color = texture(sky, p.d.xyz) + textureLod(skies, vec4(p.e.xyz, 1.0), 0.5);
	second = vec4(texture(shadow, vec4(p.f.xyz, p.d.w)), texture(sky, p.f.xyz, 0.25).yw, textureLod(skies, vec4(p.d.xyz, 2.4), 1.0).z);
}
