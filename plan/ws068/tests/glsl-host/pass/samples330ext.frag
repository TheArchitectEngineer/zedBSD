#version 330 core
#extension GL_ARB_sample_shading : require
// GLSL 3.30 with GL_ARB_sample_shading: the sample shaded and its position, the mask written.
layout(location = 0) out vec4 colour;
void main()
{
	colour = vec4(gl_SamplePosition, float(gl_SampleID) * 0.25, 1.0);
	gl_SampleMask[0] = 1 << gl_SampleID;
}
