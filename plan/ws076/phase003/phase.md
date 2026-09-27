<!-- awesome-plan project=zedbsd record=ws076p003 -->

# ws076-p003: exp・exp2・expm1・log・log2・log10・log1p（double-double、表の生成）

Phase ID: `ws076-p003`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（main の指示でサブエージェント LIBM が worktree の branch で実行）

## 範囲

[design.md](../design.md) §4・§5・§7 のうち: double-double の基本演算、`__libm_scale`、表と定数の生成、exp の族と log の族
（double と float、long double の wrapper）。pow が使う kernel（dd の引数の exp、2^-78 の log）を含む。

## 受け入れ

1. 各関数の最大誤差が 1 ulp 未満、正確に表せる結果（exp2(n)、log2(2^n)、log10(10^n)、exp(0)、log(1)）が正確、特殊な値・errno・
   例外の表が通る（host と guest）。
2. 表の生成が再現する。規約の検査 0。build（warning 0）と boot test。

## 結果（2026-09-28）

cleared。

- `math-internal.h`: `struct libm_dd` と static inline の `libm_two_sum`（Knuth）・`libm_fast_two_sum`（Dekker）・
  `libm_two_product`（Veltkamp の分割と Dekker の積、`__FP_FAST_FMA` の target は `__builtin_fma`）・`libm_dd_add`・
  `libm_dd_add_double`・`libm_dd_multiply`・`libm_dd_multiply_double`・`libm_dd_divide`・`libm_dd_sqrt`。
- `support.c` の `__libm_scale`: dd × 2^n を 1 回だけ丸める。subnormal は 2^(n+1022) 倍して ±1 に足し、[1,2) の格子（2^-52）で
  丸めてから 1 を引く（二重の丸めが無い）。正確な subnormal では underflow を立てない。
- `exp.c`: Tang の表の方式（2^(j/128) の dd の表、ln2/128 を 35・53・53 bit に分割）、exp(r)−1 は Taylor の 7 次（r²/2 まで dd）。
  `__libm_exp_dd`（dd の引数、pow・双曲線が使う）、`__libm_expm1_kernel`。exp2 は 1/128 の倍数を正確に引く。expm1 は |x| < 0.0027 で
  kernel を直接、その外は exp − 1 を dd で。
- `log.c`: m ∈ [0.709, 1.418) を encoding の 128 区間に分け、区間 74 は 1 が中央で c = 1。r = m·c − 1 は two_product で正確な dd、
  log(1+r) は Taylor の 11 次（r³/3 まで dd）。`__libm_log_parts`・`__libm_log_dd`。log2 = e + log(m)/ln2、log10 = log/ln10 を dd で、
  log1p は |x| < 2^-27 で級数、その外は log(hi) + lo/hi。
- `gen/gen-tables.py`（mpmath 256 bit）が `tables.c`・`math-constants.h` を書く。再実行で同じ出力（`cmp` で確かめた）。|r| の最大は
  2^-8.000。
- float の版（expf・exp2f・expm1f・logf・log2f・log10f・log1pf）は double から 1 回、long double は double と同じ。legacy.c から
  旧 exp・log の族を消した。

## 検証

host（`plan/tools/libm/host-test.sh`、MPFR の参照）。「host max」は同じ入力での host の glibc 2.41 の最大誤差（ctypes で呼ぶ）。

| 関数 | 件数 | 最大 ulp | 平均 ulp | >0.5 ulp | host（glibc）の最大 |
| --- | --- | --- | --- | --- | --- |
| exp | 400007 | 0.5000 | 0.2264 | 0 | 1.0000 |
| exp2 | 402098 | 0.5000 | 0.1939 | 0 | 1.0000 |
| expm1 | 440000 | 0.5000 | 0.2221 | 0 | 0.7800 |
| expf | 400000 | 0.5000 | 0.2178 | 0 | 0.5013 |
| exp2f | 400000 | 0.5000 | 0.1847 | 0 | 0.5016 |
| expm1f | 400000 | 0.5000 | 0.1319 | 0 | 0.5000 |
| log | 400003 | 0.5000 | 0.2406 | 0 | 0.5119 |
| log2 | 402100 | 0.5000 | 0.2425 | 0 | 0.5367 |
| log10 | 400025 | 0.5000 | 0.2431 | 0 | 1.5637 |
| log1p | 400000 | 0.5000 | 0.2102 | 0 | 0.7623 |
| logf | 400000 | 0.5000 | 0.2499 | 0 | 0.7300 |
| log2f | 400000 | 0.5000 | 0.2460 | 0 | 0.6413 |
| log10f | 400000 | 0.5000 | 0.2495 | 0 | 0.5000 |
| log1pf | 375151 | 0.5000 | 0.0579 | 0 | 0.5000 |

- 全部の入力で正しく丸めた（0.5 ulp を超えた件数 0）。exp2(n)（n = −1074〜1023）、log2(2^n)、log10(10^n)（n = 0〜22）、exp(±0)、
  log(1) は正確。特殊な値（exp・exp2・expm1・log・log2・log10・log1p の 27 件を足して 75 件）は全部一致。p002 の群 A も全部一致のまま。
- guest（QEMU、amd64、`guest-test.sh`、各 2000 件）: 同じ関数が最大 0.5000 ulp 以下、特殊な値 0 failed、`libm-test: PASS`。
- 規約: 新しい file で `style-check.py` 0。build: amd64 の lean image が warning 0、i386（soft float）・arm64 の compile も通る。
- boot test: `build/ws076-boot-p003/login.png` PASS。
- 試験の道具の修正: gen-reference.py の fmod の入力の生成で指数 1024 を作る誤り、host の glibc の誤差の列（`.host` の side file）、
  `GENERATORS=exp,log` で族を絞る。
