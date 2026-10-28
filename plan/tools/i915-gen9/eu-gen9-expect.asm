/* ws229-p003: the Gen9 listing i915-vk-gentool-eu.c -gen9 must encode, reviewed line by line against the */
/* calls of that program (2026-10-14); eu-gen9-host.sh assembles it with brw_asm --gen=skl and compares bytes. */
mov(8)          g16<1>F         0x3edc28f6F      /* 0.43F */    { align1 1Q };
mov(8)          g17<1>F         g3<8,8,1>F                      { align1 1Q };
mov(8)          g18<1>F         g2.1<0,1,0>F                    { align1 1Q };
mov(8)          g19<1>F         -g17<8,8,1>F                    { align1 1Q };
mov(8)          g127<1>UD       g1<8,8,1>UD                     { align1 1Q };
mov(8)          g100<1>D        0D                              { align1 1Q };
add(8)          g20<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
add(8)          g21<1>F         g16<8,8,1>F     -g17<8,8,1>F    { align1 1Q };
mul(8)          g22<1>F         g4.3<0,1,0>F    g3<8,8,1>F      { align1 1Q };
mul(8)          g23<1>F         g22<8,8,1>F     0x3f99999aF  /* 1.2F */ { align1 1Q };
math sin(8)     g24<1>F         g23<8,8,1>F     null<8,8,1>F    { align1 1Q };
math cos(8)     g25<1>F         g23<8,8,1>F     null<8,8,1>F    { align1 1Q };
math rsq(8)     g26<1>F         g23<8,8,1>F     null<8,8,1>F    { align1 1Q };
sends(8)        g40UD           g30UD           g31UD           0x02420001                0x00000040
                            sampler MsgDesc: sample SIMD8 Surface = 1 Sampler = 0  mlen 1 ex_mlen 1 rlen 4 { align1 1Q };
sends(8)        nullUD          g1UD            g100UD          0x02080007                0x00000200
                            urb MsgDesc: offset 0 SIMD8 write  mlen 1 ex_mlen 8 rlen 0 { align1 1Q };
sends(8)        nullUD          g127UD          g123UD          0x02080027                0x00000100
                            urb MsgDesc: offset 2 SIMD8 write  mlen 1 ex_mlen 4 rlen 0 { align1 1Q EOT };
sendc(8)        null<1>UW       g124<0,1,0>UD   0x88031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0  mlen 4 rlen 0 { align1 1Q EOT };
cmp.l.f0.0(8)   g27<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
cmp.ge.f0.0(8)  g28<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
cmp.z.f0.0(8)   g29<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
cmp.nz.f0.0(8)  g30<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
cmp.nz.f0.0(8)  null<1>D        g27<8,8,1>D     0D              { align1 1Q };
(+f0.0) sel(8)  g31<1>D         g16<8,8,1>D     g17<8,8,1>D     { align1 1Q };
mov(1)          f1<1>UW         g1.14<0,1,0>UW                  { align1 WE_all 1N };
(+f1.0) cmp.z.f1.0(8) null<1>D  g28<8,8,1>D     0D              { align1 1Q };
sel.l(8)        g32<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
sel.ge(8)       g33<1>F         g16<8,8,1>F     g17<8,8,1>F     { align1 1Q };
and(8)          g34<1>D         g27<8,8,1>D     g28<8,8,1>D     { align1 1Q };
or(8)           g35<1>D         g27<8,8,1>D     g28<8,8,1>D     { align1 1Q };
not(8)          g36<1>D         g27<8,8,1>D                     { align1 1Q };
rndd(8)         g37<1>F         g16<8,8,1>F                     { align1 1Q };
frc(8)          g38<1>F         g16<8,8,1>F                     { align1 1Q };
mov(8)          g39<1>F         (abs)g16<8,8,1>F                { align1 1Q };
math inv(8)     g44<1>F         g16<8,8,1>F     null<8,8,1>F    { align1 1Q };
math sqrt(8)    g45<1>F         g16<8,8,1>F     null<8,8,1>F    { align1 1Q };
math log(8)     g46<1>F         g16<8,8,1>F     null<8,8,1>F    { align1 1Q };
math exp(8)     g47<1>F         g16<8,8,1>F     null<8,8,1>F    { align1 1Q };

LABEL0:
add(8)          g48<1>D         g16<8,8,1>D     -g17<8,8,1>D    { align1 1Q };
xor(8)          g49<1>D         g16<8,8,1>D     g17<8,8,1>D     { align1 1Q };
shl(8)          g50<1>D         g16<8,8,1>D     g17<8,8,1>D     { align1 1Q };
shr(8)          g51<1>UD        g16<8,8,1>UD    0x00000005UD    { align1 1Q };
asr(8)          g52<1>D         g16<8,8,1>D     g17<8,8,1>D     { align1 1Q };
mul(8)          g53<1>D         g16<8,8,1>D     g17<16,8,2>UW   { align1 1Q };
mul(8)          g54<1>D         g16<8,8,1>D     g17.1<16,8,2>UW { align1 1Q };
rndz(8)         g55<1>F         g16<8,8,1>F                     { align1 1Q };
mov(8)          g56<1>D         g16<8,8,1>F                     { align1 1Q };
mov(8)          g57<1>F         g16<8,8,1>D                     { align1 1Q };
mov(8)          g58<1>UD        g16<8,8,1>F                     { align1 1Q };
mov(8)          g59<1>F         g16<8,8,1>UD                    { align1 1Q };
cmp.ge.f0.0(8)  g60<1>D         g16<8,8,1>UD    g17<8,8,1>UD    { align1 1Q };
(+f0.0) add(8)  g61<1>D         g61<8,8,1>D     -g17<8,8,1>D    { align1 1Q };
(+f0.0) or(8)   g62<1>D         g62<8,8,1>D     -2147483648D    { align1 1Q };
cmp.nz.f0.0(8)  null<1>D        g60<8,8,1>D     0D              { align1 1Q };
(+f0.0) while(8) JIP:  LABEL0                                   { align1 1Q };
math intdiv(8)  g63<1>D         g16<8,8,1>D     g17<8,8,1>D     { align1 1Q };
math intmod(8)  g64<1>D         g16<8,8,1>D     g17<8,8,1>D     { align1 1Q };
math intdiv(8)  g65<1>UD        g16<8,8,1>UD    g17<8,8,1>UD    { align1 1Q };
math intmod(8)  g66<1>UD        g16<8,8,1>UD    g17<8,8,1>UD    { align1 1Q };
rnde(8)         g67<1>F         g16<8,8,1>F                     { align1 1Q };
mov(8)          g95<1>UD        0x00000000UD                    { align1 WE_all 1Q };
and(1)          g95.3<1>UD      g0.3<0,1,0>UD   0x0000000fUD    { align1 WE_all 1N };
and(1)          g95.5<1>UD      g0.5<0,1,0>UD   0xfffffc00UD    { align1 WE_all 1N };
mov(1)          g95.2<1>UD      0x00000006UD                    { align1 WE_all 1N };
sends(8)        nullUD          g95UD           g20UD           0x020a02fd                0x00000040
                            data MsgDesc: (DC OWORD block write, bti 253, owords = 2, aligned = 0)  mlen 1 ex_mlen 1 rlen 0 { align1 1Q };
send(8)         g21<1>UW        g95<0,1,0>UD    0x021802fd
                            data MsgDesc: (DC OWORD block read, bti 253, owords = 2, aligned = 0)  mlen 1 rlen 1 { align1 WE_all 1Q };
(+f1.0) sendc(8) null<1>UW      g124<0,1,0>UD   0x88031400
                            render MsgDesc: RT write SIMD8 LastRT Surface = 0  mlen 4 rlen 0 { align1 1Q EOT };
