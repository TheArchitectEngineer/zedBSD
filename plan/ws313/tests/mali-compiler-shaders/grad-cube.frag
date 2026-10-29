#version 450
// The Mali compiler's host check: textureGrad of a cube, a cube array and a cube shadow sampler (the level computed from the derivatives).
layout(set = 0, binding = 0) uniform samplerCube sky;
layout(set = 0, binding = 1) uniform samplerCubeArray skies;
layout(set = 0, binding = 2) uniform samplerCubeShadow shadow;
layout(location = 0) in vec4 v;
layout(location = 1) in vec4 dx;
layout(location = 2) in vec4 dy;
layout(push_constant) uniform P { vec4 d; } p;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
void main()
{
	color = textureGrad(sky, v.xyz - 1.0, dx.xyz, dy.yzw) + textureGrad(skies, vec4(p.d.xyz, 1.0), dx.wzy, dy.xyz * 0.25);
	second = vec4(textureGrad(shadow, vec4(v.zxy - 0.5, v.w * 0.5), dx.xyz * 0.01, dy.zyx));
}
