#version 450
// ws031-p024: 40 selections nested, past the construct limit: refused (constructs nested too deep)
layout(location = 0) in vec4 pixel;
layout(location = 0) out vec4 color;
void main()
{
    int x = int(pixel.x);
    int acc = 0;
    if ((x & 1) != 0) {
        acc += 1;
        if ((x & 2) != 1) {
            acc += 1;
            if ((x & 4) != 0) {
                acc += 1;
                if ((x & 8) != 1) {
                    acc += 1;
                    if ((x & 16) != 0) {
                        acc += 1;
                        if ((x & 32) != 1) {
                            acc += 1;
                            if ((x & 64) != 0) {
                                acc += 1;
                                if ((x & 128) != 1) {
                                    acc += 1;
                                    if ((x & 256) != 0) {
                                        acc += 1;
                                        if ((x & 512) != 1) {
                                            acc += 1;
                                            if ((x & 1024) != 0) {
                                                acc += 1;
                                                if ((x & 2048) != 1) {
                                                    acc += 1;
                                                    if ((x & 4096) != 0) {
                                                        acc += 1;
                                                        if ((x & 8192) != 1) {
                                                            acc += 1;
                                                            if ((x & 16384) != 0) {
                                                                acc += 1;
                                                                if ((x & 32768) != 1) {
                                                                    acc += 1;
                                                                    if ((x & 1) != 0) {
                                                                        acc += 1;
                                                                        if ((x & 2) != 1) {
                                                                            acc += 1;
                                                                            if ((x & 4) != 0) {
                                                                                acc += 1;
                                                                                if ((x & 8) != 1) {
                                                                                    acc += 1;
                                                                                    if ((x & 16) != 0) {
                                                                                        acc += 1;
                                                                                        if ((x & 32) != 1) {
                                                                                            acc += 1;
                                                                                            if ((x & 64) != 0) {
                                                                                                acc += 1;
                                                                                                if ((x & 128) != 1) {
                                                                                                    acc += 1;
                                                                                                    if ((x & 256) != 0) {
                                                                                                        acc += 1;
                                                                                                        if ((x & 512) != 1) {
                                                                                                            acc += 1;
                                                                                                            if ((x & 1024) != 0) {
                                                                                                                acc += 1;
                                                                                                                if ((x & 2048) != 1) {
                                                                                                                    acc += 1;
                                                                                                                    if ((x & 4096) != 0) {
                                                                                                                        acc += 1;
                                                                                                                        if ((x & 8192) != 1) {
                                                                                                                            acc += 1;
                                                                                                                            if ((x & 16384) != 0) {
                                                                                                                                acc += 1;
                                                                                                                                if ((x & 32768) != 1) {
                                                                                                                                    acc += 1;
                                                                                                                                    if ((x & 1) != 0) {
                                                                                                                                        acc += 1;
                                                                                                                                        if ((x & 2) != 1) {
                                                                                                                                            acc += 1;
                                                                                                                                            if ((x & 4) != 0) {
                                                                                                                                                acc += 1;
                                                                                                                                                if ((x & 8) != 1) {
                                                                                                                                                    acc += 1;
                                                                                                                                                    if ((x & 16) != 0) {
                                                                                                                                                        acc += 1;
                                                                                                                                                        if ((x & 32) != 1) {
                                                                                                                                                            acc += 1;
                                                                                                                                                            if ((x & 64) != 0) {
                                                                                                                                                                acc += 1;
                                                                                                                                                                if ((x & 128) != 1) {
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
                                                                                                                            }
                                                                                                                        }
                                                                                                                    }
                                                                                                                }
                                                                                                            }
                                                                                                        }
                                                                                                    }
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
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
