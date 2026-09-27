<!-- awesome-plan project=zedbsd record=ws076p005 -->

# ws076-p005: 三角関数・逆三角関数・双曲線関数

Phase ID: `ws076-p005`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（main の指示でサブエージェント LIBM が worktree の branch で実行）

## 範囲

sin・cos・tan（大きな引数の縮約を含む）、asin・acos・atan・atan2、sinh・cosh・tanh・asinh・acosh・atanh と float・long double の版。
[design.md](../design.md) §5。

## 受け入れ

1. 各関数の最大誤差が 1 ulp 未満、特殊な値・errno・例外（C11 Annex F の符号付きの 0、atan2 の象限と Inf）が通る（host と guest）。
2. 表の生成が再現、規約の検査 0、build と boot test。

## 結果（2026-09-28）

cleared。

- `trig.c`: |x| ≤ π/4 は縮約なし、2^19 未満は Cody-Waite（π/2 を 33・33・33・53 bit に分割）、それ以上は Payne-Hanek（x = M·2^E の
  E に合う 2/π の 192 bit の窓を 32 bit の limb で M と掛け、整数の 2 bit が象限、小数の先頭の 1 から 53 + 64 bit を dd に。
  0.5 以上は次の象限と負の余り）。kernel は sin(i/64)・cos(i/64) の dd の表と |d| ≤ 1/128 の Taylor（sin d は d^9 まで、cos d − 1 は
  d^8 まで）を加法定理で。tan は dd の除算。|x| < 2^-27 は x（cos は 1）。
- `atrig.c`: kernel `__libm_atan_dd`（0 ≤ u ≤ 1、c = i/64 の atan の dd の表、t = (u − c)/(1 + uc) を dd、atan t は t^13 まで）。
  atan は |x| > 1 で π/2 − atan(1/x)、atan2 は大きい方の指数で両方を scalbn（subnormal も正確）し、小さい方の比で kernel、象限で
  π・π/2 を dd で。asin・acos は √((1−x)(1+x)) を dd にして、比が 1 以下になる側で kernel（打ち消しの無い形）。
- `hyperbolic.c`: E = expm1|x| の dd から sinh = (E + E/(E+1))/2、cosh = 1 + E²/(2(E+1))、tanh = E/(E+2)（E = expm1 2|x|）、
  |x| > 40 は exp(|x|)/2 を scale を 1 減らして（709.78 < |x| ≤ 710.47 も有限）。asinh・acosh・atanh は dd の log（log h + l/h）、
  x² − 1 は正確な平方から。|x| > 2^28 は log|x| + ln2。
- 表（`gen-tables.py` に追加）: sin/cos の i/64（52 点）、atan の i/64（65 点）、2/π の 1280 bit、π/2 の分割、π・π/2 の dd、
  Taylor の係数。再実行で同じ出力。
- 途中で見つけて直した誤り: `libm_dd_sqrt` が 0 で 0 除算（acos(−1) で NaN の index になり segfault）→ 0 の root は 0。
- legacy.c から旧 sin〜atanh を消した。long double は double と同じ。

## 検証

host（MPFR の参照、`GENERATORS=trig,atrig,hyperbolic plan/tools/libm/host-test.sh --count 400000`）。入力は各 binade の無作為、
[−10,10]、π/2 の倍数の隣（k は 2^200 まで）、Muller の表の縮約の最悪の点 6381956970095103·2^797、subnormal、asin・acos・atanh の
±1 の近く、双曲線の overflow の境界（710.4758600739439）。

| 関数 | 件数 | 最大 ulp | >0.5 | glibc | 関数 | 件数 | 最大 ulp | >0.5 | glibc |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sin | 400005 | 0.5000 | 0 | 0.5141 | sinf | 200000 | 0.5000 | 0 | 0.5605 |
| cos | 400005 | 0.5000 | 0 | 7.9546 | cosf | 200000 | 0.5000 | 0 | 0.5593 |
| tan | 400005 | 0.5000 | 0 | 14.3606 | tanf | 200000 | 0.5000 | 0 | 0.5000 |
| asin | 350006 | 0.5000 | 0 | 0.5092 | asinf | 200000 | 0.5000 | 0 | 0.5000 |
| acos | 350006 | 0.5000 | 0 | 0.5170 | acosf | 200000 | 0.5000 | 0 | 0.5000 |
| atan | 400000 | 0.5000 | 0 | 0.5058 | atanf | 200000 | 0.5000 | 0 | 0.5000 |
| atan2 | 400000 | 0.5000 | 0 | 0.5146 | atan2f | 200000 | 0.5000 | 0 | 0.5000 |
| sinh | 300004 | 0.5000 | 0 | 1.6285 | sinhf | 200000 | 0.5000 | 0 | 0.5000 |
| cosh | 300004 | 0.5000 | 0 | 1.6285 | coshf | 200000 | 0.5000 | 0 | 0.5000 |
| tanh | 300004 | 0.5000 | 0 | 1.8998 | tanhf | 200000 | 0.5000 | 0 | 0.5000 |
| asinh | 400000 | 0.5000 | 0 | 1.3460 | asinhf | 200000 | 0.5000 | 0 | 0.5000 |
| acosh | 400002 | 0.5000 | 0 | 1.8475 | acoshf | 200000 | 0.5000 | 0 | 0.5000 |
| atanh | 350000 | 0.5000 | 0 | 1.5666 | atanhf | 200000 | 0.5000 | 0 | 0.5000 |

- 全部の入力で正しく丸めた（0.5 ulp を超えた件数 0）。「glibc」は同じ入力での host の glibc 2.41 の最大誤差（参考。cos・tan の大きな
  値は π/2 の倍数の近くの入力で、未解析）。特殊な値は 48 件を足して計 153 件が全部一致。p002〜p004 の回帰（各 20000 件）も PASS。
- guest（QEMU、amd64、image の libc.so、各 2000 件）: 26 関数が最大 0.5000 ulp 以下、特殊な値 0 failed、PASS。
- 規約 0、amd64 の build warning 0、i386・arm64 の compile も通る。boot test（`build/ws076-boot-p005/login.png`）PASS。
