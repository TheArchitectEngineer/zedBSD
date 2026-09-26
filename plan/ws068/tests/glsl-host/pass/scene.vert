// egltest's scene (ws068-p008) in GLSL ES 1.00: a position, a colour and texture coordinates per vertex;
// the matrix places each shape, the tint scales its colour.
attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_uv;
varying vec4 v_color;
varying vec2 v_uv;
uniform mat4 u_matrix;
uniform vec4 u_tint;

void main()
{
	v_color = a_color * u_tint;
	v_uv = a_uv;
	gl_Position = u_matrix * a_position;
}
