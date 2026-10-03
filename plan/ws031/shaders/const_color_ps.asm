// Gen12 SIMD8 fragment shader: writes a constant colour to render target 0.
//
// The payload of a SIMD8 single-source render target write is four consecutive
// registers holding red, green, blue and alpha.  A send that ends the thread
// must take its payload from the high register file, so the colours are built
// in r112-r115.  The send runs under the pixel dispatch mask, so only covered
// pixels are written, and it retires the thread.
(W) mov (8|M0)     r112.0<1>:f   0x3f800000:f
(W) mov (8|M0)     r113.0<1>:f   0x0:f
(W) mov (8|M0)     r114.0<1>:f   0x0:f
(W) mov (8|M0)     r115.0<1>:f   0x3f800000:f
    send.render (8|M0)  null   r112   null   0x00000000   0x08031400   {EOT,@1}
