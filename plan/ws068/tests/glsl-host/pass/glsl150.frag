#version 150 compatibility
in vec2 v_uv;
in Data {
	vec4 colour;
} data;
uniform sampler2D u_texture;
void main()
{
	gl_FragColor = texture2D(u_texture, v_uv) * data.colour + texture(u_texture, v_uv);
}
