/* ws229-p005: the Gen9 store kernel of the compute test (drv_i915_test_eu_kernel_gen9 in */
/* src/drivers/gpu/i915/tests/execution/eu-test.c); eu-test-gen9-host.sh assembles it with brw_asm --gen=skl. */
mov(8)          g5<1>UD         0xc0ffee02UD                    { align1 1Q };
mov(8)          g127<1>UD       g0<8,8,1>UD                     { align1 WE_all 1Q };
mov(8)          g1.1<2>UD       0x00000001UD                    { align1 WE_all 1Q };
mov(8)          g1<2>UD         0x00400c20UD                    { align1 WE_all 1Q };
mov(8)          g3.1<2>UD       g1.1<0,1,0>UD                   { align1 1Q };
mov(8)          g3<2>UD         g1<0,1,0>UD                     { align1 1Q };
send(8)         null<1>UW       g3<0,1,0>UD     0x06066efd
                            dp data 1 MsgDesc: (DC A64 untyped surface write, Surface = 253, SIMD8, Mask = 0xe)  mlen 3 rlen 0 { align1 1Q };
send(8)         null<1>UW       g127<0,1,0>UD   0x82000010
                            thread_spawner MsgDesc: mlen 1 rlen 0           { align1 WE_all 1Q EOT };
nop ;
