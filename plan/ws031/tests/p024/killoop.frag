#version 450
// ws031-p024: a discard inside a loop with a per-pixel trip count: accepted
layout(location = 0) in vec4 pixel;
layout(location = 0) out vec4 color;
void main()
{
    int x = int(pixel.x);
    int acc = 0;
    for (int i = 0; i < (x & 7); i++) {
        if (i == (int(pixel.y) & 7))
            discard;
        acc += i;
    }
    color = vec4(float(acc));
}
