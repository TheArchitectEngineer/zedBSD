#version 450
// ws031-p024: integer division and remainder by a run-time zero and INT_MIN / -1 (undefined values, no refusal): accepted
layout(location = 0) in vec4 pixel;
layout(location = 0) out vec4 color;
void main()
{
    int x = int(pixel.x);
    int zero = x - x;
    int minimum = int(0x80000000u) + zero;
    int r = x / zero;
    r ^= x % zero;
    r ^= int(uint(x) / uint(zero));
    r ^= int(uint(x) % uint(zero));
    r ^= minimum / (zero - 1);
    r ^= minimum % (zero - 1);
    color = vec4(float(r & 255));
}
