#version 450
// ws031-p024: nine loops nested, past the limit: refused (loops nested too deep)
layout(location = 0) in vec4 pixel;
layout(location = 0) out vec4 color;
void main()
{
    int acc = 0;
    int x = int(pixel.x);
    for (int i0 = 0; i0 < (x & 3) + 1; i0++) {
        for (int i1 = 0; i1 < (x & 3) + 1; i1++) {
            for (int i2 = 0; i2 < (x & 3) + 1; i2++) {
                for (int i3 = 0; i3 < (x & 3) + 1; i3++) {
                    for (int i4 = 0; i4 < (x & 3) + 1; i4++) {
                        for (int i5 = 0; i5 < (x & 3) + 1; i5++) {
                            for (int i6 = 0; i6 < (x & 3) + 1; i6++) {
                                for (int i7 = 0; i7 < (x & 3) + 1; i7++) {
                                    for (int i8 = 0; i8 < (x & 3) + 1; i8++) {
                                        acc += 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    color = vec4(float(acc));
}
