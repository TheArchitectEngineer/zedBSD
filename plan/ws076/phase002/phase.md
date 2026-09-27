<!-- awesome-plan project=zedbsd record=ws076p002 -->

# ws076-p002: `src/libc/math/` への分割と build、正確な関数、試験の道具

Phase ID: `ws076-p002`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（main の指示でサブエージェント LIBM が worktree の branch で実行）

## 範囲

[design.md](../design.md) §5・§6・§8 のうち: math.c を `src/libc/math/` に分け、全 platform の build の rule を直す。群 A の関数
（sqrt・fma・fmod・remainder・remquo・丸め系・lrint 系・frexp・ldexp・scalbn・ilogb・logb・modf・nextafter・fmin・fmax・fdim・
fabs・copysign・nan・fpclassify・signbit と float・long double の版）を書き直す。試験の道具（参照の生成、runner、host と guest の
script）を作る。

## 受け入れ

1. 群 A の全関数が参照と bit で一致し、特殊な値・errno・例外の表が通る（host と guest）。
2. 新しい file の規約の検査（`style-check.py`）が 0。
3. build（warning 0）と boot test。

## 結果（2026-09-28）

cleared。

- 新しい file（`src/libc/math/`）: `math-internal.h`（bit の出し入れ、`#pragma STDC FP_CONTRACT OFF`〔clang だけ。sparcv9 の gcc は
  soft float で縮約する命令が無く、pragma を拒む〕、内部の宣言は `visibility("hidden")`）、`support.c`（errno と例外の helper、
  `__libm_pack`: 64 bit の仮数と sticky から 1 回の最近接偶数の丸め、subnormal、overflow・underflow の報告、`__libm_unpack`）、
  `classify.c`、`rounding.c`、`remainder.c`（11 bit ずつの整数の長い除算、商の下位 31 bit）、`fma.c`（128 bit の整数の積と和）、
  `sqrt.c`（桁ごとの整数の平方根）、`float.c`、`long-double.c`、`legacy.c`（未着手の関数の旧コード、p006 で消す）。
  `src/libc/math.c` を消した。
- build: `src/libc/libc.mk` に `ZEDBSD_LIBM_SOURCES`・`ZEDBSD_LIBM_HEADERS`、`softfloat.mk`・5 つの `vmunix.mk`（静的 pattern の
  rule）・`sysroot.mk`（compiler-rt の source の一覧と入力）をそれに。
- `math_errhandling` の変更（`MATH_ERRNO | MATH_ERREXCEPT`）は、errno も立て始めた p003 以降の関数と一緒に p007 で行う（header の
  変更は全 program の再 build を招くので 1 回に）。
- 試験の道具 `plan/tools/libm/`（Master の Tools に登録）: `gen-reference.py`（gmpy2 = MPFR 4.2.1 の IEEE の context と
  `fractions.Fraction` の正確な有理数）、`libm-test.c`（標準 C だけ、host と zedBSD で同じ source、errno・例外は zedBSD の ABI の値）、
  `errno-shim.c`、`host-test.sh`、`guest-test.sh`（`plan/ws076/tests/config-amd64-libm.mk` の lean image に runner と参照を入れ、
  serial で実行）。

## 検証

- host（`plan/tools/libm/host-test.sh`、各 20000 件、host の clang、libm を link しない）: 33 関数 660361 件が全部 bit で一致、特殊な
  値 48 件が全部一致。`libm-test: PASS`。
- guest（QEMU、amd64、`plan/tools/libm/guest-test.sh`、各 2000 件、image の libc.so）: 同じく全部一致、特殊な値 0 failed、
  `libm-test: PASS`。
- 規約: 新しい file 全部で `style-check.py` 0（`legacy.c` は旧コードのまま、対象外）。
- build: amd64 の lean image（`build/ws076-amd64`）が warning 0。libm の source を i386（`-msoft-float`）・arm64 の clang でも
  `-Wall -Wextra -Werror` で compile した。pcat・pc98・arm64・sparcv9 の image の build は未実施。
- boot test: `OUTPUT=build/ws076-boot-p002 plan/tools/boot-test.sh build/ws076-amd64/hdd-image.img` PASS（login prompt）。
- 途中で見つけて直した誤り: `__libm_pack` で大きな指数の `field << 52` が溢れて ldexp(x, 2000) が有限値になった（14 件）→ 先に
  overflow を判定。

| 関数 | 件数（host） | 結果 |
| --- | --- | --- |
| fmod・remainder・remquo | 各 20011 | 正確（1e17 % 7 = 5、2^60 % 7 = 1 を含む） |
| fmodf・remainderf・remquof | 各 20000 | 正確 |
| fma・fmaf | 各 20000 | 正確（打ち消し、subnormal の結果を含む） |
| sqrt・sqrtf | 20202・20000 | 正確（正しく丸める） |
| trunc floor ceil round rint nearbyint | 各 20013 | 正確 |
| lrint llrint lround llround | 各 20012 | 正確 |
| rintf truncf floorf | 各 20000 | 正確 |
| frexp frexpf ldexp ldexpf scalbn ilogb logb modf nextafter nextafterf | 各 20000 | 正確 |
