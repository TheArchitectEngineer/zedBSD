#version 150
uniform sampler2DMS u_colour;
uniform isampler2DMS u_ids;
uniform int u_samples;
out vec4 o_colour;
void main()
{
	ivec2 size = textureSize(u_colour);
	vec4 sum = vec4(0.0);
	for (int sample = 0; sample < u_samples; sample++)
		sum += texelFetch(u_colour, ivec2(gl_FragCoord.xy) % size, sample);
	o_colour = sum / float(u_samples) + vec4(float(texelFetch(u_ids, ivec2(0), 0).x));
}
