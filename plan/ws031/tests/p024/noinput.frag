#version 450
// ws031-p024: a fragment shader that reads no input (the SBE with no attribute): accepted
layout(location = 0) out vec4 color;
void main()
{
    color = vec4(gl_FragCoord.x / 64.0, 0.25, 0.5, 1.0);
}
