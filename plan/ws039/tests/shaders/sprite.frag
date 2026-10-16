#version 450
// ws039-p007: the point sprite coordinate, the primitive number, and a second colour (a dual-source blend).
layout(location = 0) in vec4 tint;
layout(location = 2) flat in vec4 flat_tint;
layout(location = 0, index = 0) out vec4 color;
layout(location = 0, index = 1) out vec4 blend;
void main()
{
	color = tint * gl_PointCoord.x + vec4(gl_PointCoord.y) + flat_tint;
	blend = vec4(float(gl_PrimitiveID & 7), tint.yzw);
}
