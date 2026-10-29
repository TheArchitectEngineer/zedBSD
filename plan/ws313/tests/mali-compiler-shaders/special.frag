#version 450
// The Mali compiler's host check: transcendental functions, derivatives and the fragment's builtins.
layout(location = 0) in vec4 v;
layout(location = 1) flat in ivec2 f;
layout(location = 2) noperspective in vec2 n;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 second;
layout(push_constant) uniform P { float k; int i; } p;
void main()
{
	float a = sin(v.x * 3.0) + cos(v.y - p.k);
	float b = dFdx(v.z) + dFdyFine(v.w) + fwidth(v.x);
	vec2 c = gl_FragCoord.xy * 0.001 + n;
	float d = gl_FrontFacing ? exp2(v.z) : log2(abs(v.w) + 1.0);
	int e = f.x / (p.i | 1) + f.y % 7;
	if (v.x > 1.75)
		discard;
	color = vec4(a, b + d, c.x * c.y, float(e) * 0.01);
	second = vec4(fract(v.x * 7.5), floor(v.y * 3.0), sqrt(abs(v.z)), inversesqrt(abs(v.w) + 0.5));
}
