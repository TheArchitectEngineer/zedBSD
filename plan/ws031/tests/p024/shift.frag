#version 450
// ws031-p024: shifts by 0, 31 and by run-time amounts of 32 to 63 (undefined values past 31): accepted
layout(location = 0) in vec4 pixel;
layout(location = 0) out vec4 color;
void main()
{
    int x = int(pixel.x);
    int amount = 32 + (x & 31);
    int r = (x << amount) ^ (x >> amount) ^ int(uint(x) >> amount);
    r ^= (x << 31) ^ (x >> 0);
    color = vec4(float(r & 255));
}
