/* ws229-p005: the draw fixture's constant-colour pixel shader for Gen9 (tests/fixtures/draw-fixture.c, */
/* i915_draw_const_color_ps_gen9). brw_asm --gen=skl assembles it; plan/ws229/tests/host/run.sh compares the bytes. */
/* The Gen12 shader's SIMD8 variant, step for step: the pixel mask, the entry marker 0xc0ffee01 stored to */
/* 0x100400c10 with an A64 untyped write (the address g4-g5 and the data g6 in one message), and opaque red */
/* written headerless to render target 0 from g112-g115 with the end of the thread. */
mov(1)          f1<1>UW         g1.14<0,1,0>UW                  { align1 WE_all 1N };
mov(8)          g6<1>UD         0xc0ffee01UD                    { align1 1Q };
mov(8)          g2.1<2>UD       0x00000001UD                    { align1 WE_all 1Q };
mov(8)          g2<2>UD         0x00400c10UD                    { align1 WE_all 1Q };
mov(8)          g4.1<2>UD       g2.1<0,1,0>UD                   { align1 1Q };
mov(8)          g4<2>UD         g2<0,1,0>UD                     { align1 1Q };
(+f1.0) send(8) null<1>UW       g4<0,1,0>UD     0x06066efd
                            dp data 1 MsgDesc: (DC A64 untyped surface write, Surface = 253, SIMD8, Mask = 0xe)  mlen 3 rlen 0 { align1 1Q };
mov(8)          g112<1>UD       0x3f800000UD                    { align1 1Q };
mov(8)          g113<1>UD       0x00000000UD                    { align1 1Q };
mov(8)          g114<1>UD       0x00000000UD                    { align1 1Q };
mov(8)          g115<1>UD       0x3f800000UD                    { align1 1Q };
sendc(8)        null<1>UW       g112<0,1,0>UD   0x88031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0  mlen 4 rlen 0 { align1 1Q EOT };
