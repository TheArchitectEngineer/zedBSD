<!-- awesome-plan project=zedbsd record=ws076 -->

# WS076: libc の libm を自前で正しく書き直す（src/libc）

<!-- awesome-plan-current:start -->
Status: completed
Completed: 2026-09-28（サブエージェント LIBM が worktree の branch で p001〜p007 を実行、main が統合）
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-09-28 ユーザー:「libmは独自に書いてください。libcのツリーに入れてください。」

[BUG-078](../bugs/BUG-078.md) の libm（旧 `src/libc/math.c`）の精度と正しさの問題を、既存の libm を取り込まずに自前の実装で直す。
置き場は libc の tree（`src/libc/math/`）。方式は [design.md](design.md)。

## 完了の条件（p001 で確定）と結果

| 条件 | 結果 |
| --- | --- |
| 群 A（sqrt・fma・fmod・remainder・remquo・丸め系・frexp/ldexp 系・分類系）が正確 | host 各 20000 件、guest 各 2000 件で全部 bit で一致（`1e17 % 7` = 5、`2^60 % 7` = 1） |
| 群 B（exp〜hypot の 26 関数と float 版）が 1 ulp 以内、正確に表せる結果は正確 | host 20万〜40万件で全部 0.5000 ulp 以下（0.5 を超えた件数 0 = 全件で正しく丸めた）。`pow(14, 2)` = 196、整数の冪、exp2(n)、log2(2^n)、log10(10^n)、cbrt(n³)、hypot の 3 つ組は正確 |
| 群 C（erf・erfc・tgamma・lgamma）が 1 ulp 以内 | erf 0.513、erfc 0.520、tgamma 0.616、lgamma 0.504 ulp（host 20万件）。float 版は 0.5000 |
| 特殊な値・errno・例外が C11 Annex F・POSIX のとおり | 182 件の表（値・errno・例外）が host と guest で全部一致。`math_errhandling` は `MATH_ERRNO \| MATH_ERREXCEPT` |
| host の高精度の参照との照合 | `plan/tools/libm/host-test.sh`（MPFR）と `guest-test.sh`（QEMU、amd64）が PASS |
| ブラウザの JS の `%`・`**`・Math.* が Chromium と一致 | guest で operators.js が除外なしで pass、`plan/tools/libm/js/libm.js` の 8 行が pass |
| 規約の全文の確認 | `src/libc/math/` と試験の C で `style-check.py` 0、§11 の return の形を直した |

同じ入力での host の glibc 2.41 の最大誤差（参考）: exp・exp2 1.0、log10 1.56、cos 7.95、tan 14.4、sinh 1.63、tanh 1.90、acosh 1.85、
cbrt 3.1、tgamma 5.6、lgamma 2.8 ulp。

## 結果

- `src/libc/math/`（旧 `src/libc/math.c` を消した）: `math-internal.h`（bit の出し入れ、double-double の基本演算、`#pragma STDC
  FP_CONTRACT OFF`）、`support.c`（errno と例外、1 回の丸めの pack、dd × 2^n を subnormal まで 1 回で丸める scale）、`classify.c`、
  `rounding.c`、`remainder.c`、`fma.c`、`sqrt.c`、`exp.c`、`log.c`、`pow.c`、`trig.c`（Cody-Waite と整数の Payne-Hanek）、`atrig.c`、
  `hyperbolic.c`、`cbrt-hypot.c`、`erf.c`、`gamma.c`、`bessel.c`、`float.c`、`long-double.c`、生成物の `tables.c`・`math-constants.h`。
- 係数と表の生成: `src/libc/math/gen/gen-tables.py`（mpmath 256 bit、erf・erfc は sollya の fpminimax。再実行で同じ出力、約 3 分）。
- build: `src/libc/libc.mk` の `ZEDBSD_LIBM_SOURCES`・`ZEDBSD_LIBM_HEADERS` を softfloat.mk、5 つの platform の vmunix.mk、sysroot.mk が
  使う。amd64 の image、pcat（i386）と rpi4（arm64）の libc.so を build した。
- 試験の道具: `plan/tools/libm/`（Master の Tools）: `gen-reference.py`、`libm-test.c`、`host-test.sh`、`guest-test.sh`、
  `browser-js.sh`・`js-reference.py`・`js/libm.js`（ブラウザの JS を guest で Chromium と比べる）、image の config 2 つ。

## 制限・移管

- lgamma の負の零点の近く（−2〜−18 の整数の隣）は絶対誤差 2^-66 程度で、相対誤差は保証しない。
- Bessel（j0・j1・jn・y0・y1・yn）は目標の外: 絶対誤差は小さいが零点の近くの相対誤差が大きい → [F-047](../future-work.md)。
- binary128 の long double（arm64・sparcv9）の関数は double の精度 → [F-046](../future-work.md)。amd64・i386 は long double = double。
- 実行の試験は amd64 だけ。i386（soft float）と arm64（FMA の経路）は build だけ確かめた。pc98・sparcv9 は未 build。
- 丸めの mode は最近接だけ（fenv が他を持たない）。群 B・C の FE_INEXACT は立てない場合がある（Annex F が許す）。
- V8（Chromium）自身の Math.acosh(2)・Math.atanh(0.5) は 0.6 ulp ずれており、libc の正しく丸めた値と違う（libm.js から外した）。
- ブラウザの `plan/ws074/tests/run-js-tests.py` の `GUEST_KNOWN` の BUG-078 の除外はもう要らない（WS074 の file なので main へ）。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws076-p001 | 設計（[design.md](design.md)） | cleared |
| ws076-p002 | `src/libc/math/` への分割と build、群 A の正確な関数、試験の道具 | cleared |
| ws076-p003 | exp・exp2・expm1・log・log2・log10・log1p（double-double、表の生成） | cleared |
| ws076-p004 | pow | cleared |
| ws076-p005 | 三角・逆三角・双曲線・逆双曲線 | cleared |
| ws076-p006 | cbrt・hypot・erf・erfc・lgamma・tgamma、Bessel、float・long double、legacy.c の削除 | cleared |
| ws076-p007 | 規約の全文の照合、math_errhandling、ブラウザの operators.js、他の platform の build | cleared |

## 記録の所在

各 Phase の記録（関数ごとの ulp の表、検証の詳細）は WS の完了で削除した。git の commit `505938e6` の `plan/ws076/phase00N/phase.md`
にある。
