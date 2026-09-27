#version 150
// A geometry shader passing each triangle through, with a colour made of its inputs.
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in float v_value[];
in Data {
	vec2 corner;
} data[];
out vec4 g_colour;
flat out int g_primitive;
void main()
{
	int i;
	for (i = 0; i < gl_in.length(); i++) {
		gl_Position = gl_in[i].gl_Position;
		g_colour = vec4(0.0, v_value[0] + v_value[1] + v_value[2] + 0.25, 0.0, 1.0);
		if (distance(data[i].corner, gl_in[i].gl_Position.xy) > 0.001)
			g_colour = vec4(1.0, 0.0, 0.0, 1.0);
		g_primitive = gl_PrimitiveIDIn;
		EmitVertex();
	}
	EndPrimitive();
}
