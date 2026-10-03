#version 300 es
in vec2 a_position;
flat out int v_instance;
flat out uvec2 v_ids;
void main()
{
	v_instance = gl_InstanceID;
	v_ids = uvec2(2u, 7u) ^ uvec2(1u, 3u);
	gl_Position = vec4(a_position, 0.0, 1.0);
}
