#version 450
// ws039-p007: storage writes and atomics in a pixel program that samples with derivatives (whole quads): the helper
// lanes must not write, a guarded store, and an atomic whose old value the colour reads.
layout(location = 0) in vec2 coordinate;
layout(location = 1) in vec4 tint;
layout(set = 0, binding = 0) uniform sampler2D colors;
layout(std430, set = 0, binding = 1) buffer Out { uint counter; uint words[]; };
layout(location = 0) out vec4 color;
void main()
{
	vec4 c = texture(colors, coordinate);
	uint slot = uint(gl_FragCoord.x * 7.0 + gl_FragCoord.y) & 63u;
	words[slot] = floatBitsToUint(c.x + tint.x);
	uint old = atomicAdd(counter, 1u);
	if (tint.y > 0.5)
		words[64u + (old & 63u)] = old + slot;
	color = c + vec4(float(old & 255u));
}
