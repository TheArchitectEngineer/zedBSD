#version 450
// line: the segment (x, y) from the start, 1 / its length squared, half the thickness.
layout(location = 0) noperspective in vec2 local;
layout(location = 1) flat in vec4 line;
layout(location = 2) flat in vec4 color;
layout(location = 0) out vec4 frag;
void main()
{
	float h = clamp(dot(local, line.xy) * line.z, 0.0, 1.0);
	float d = length(local - line.xy * h) - line.w;
	float a = color.a * (1.0 / 255.0) * clamp(0.5 - d, 0.0, 1.0);
	frag = vec4(color.rgb * (a / 255.0), a);
}
