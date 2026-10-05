// ws068-p004: gl_DepthRange in a vertex shader, and the point size the fragment shader's gl_PointCoord needs.
attribute vec4 a_position;
varying float v_depth;

void main()
{
	v_depth = gl_DepthRange.near + gl_DepthRange.diff * 0.5;
	gl_PointSize = 8.0;
	gl_Position = a_position;
}
