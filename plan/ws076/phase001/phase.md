<!-- awesome-plan project=zedbsd record=ws076p001 -->

# ws076-p001: 設計（関数の一覧と今の状態、精度の目標、方式、試験）

Phase ID: `ws076-p001`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（2026-09-28 main の指示でサブエージェント LIBM が worktree の branch で実行。main の Queue への反映は統合する main の session）

## 範囲

対象の関数の一覧と今の実装の状態、精度の目標、方式（引数の縮約・多項式・表・double-double）、特殊な値と errno・例外、file の構成と
build、係数の生成、試験（host の高精度の参照、ulp の計測、guest）を決める。コードは変えない。

## 受け入れ

1. [design.md](../design.md) に上の全部がある。
2. 既存の libm の source を使わない方式である（算法の出典は論文と教科書）。
3. ws.md の Phase の表と完了の条件が確定する。

## 結果（2026-09-28）

cleared。[design.md](../design.md)。

- 調べた事実（§1）: math.c が 6 箇所で compile される（softfloat.mk、5 つの vmunix.mk、sysroot.mk）。double は全 platform で
  binary64 の最近接。**arm64 の clang は既定で a*b+c を fmadd に縮約する**ので全 file に `#pragma STDC FP_CONTRACT OFF`
  （pragma が効くことを確かめた）。fenv は software の cell で丸めは最近接だけ、例外は明示して立てる。long double は amd64・i386 が
  binary64、arm64・sparcv9 が binary128。
- 目標（§2）: 群 A（sqrt・fma・fmod・remainder・remquo・丸め系・分類系）は正確、群 B（exp〜hypot の 26 関数）は double-double で
  1 回丸め（1 ulp 以内を保証、測定の最大 0.51 ulp 程度、正確に表せる結果は正確）、群 C（erf・erfc・tgamma・lgamma）は 1 ulp 以内、
  Bessel は目標の外。float は double から 1 回丸め、long double は amd64・i386 で double と同じ、binary128 は制限。
- `math_errhandling` を `MATH_ERRNO | MATH_ERREXCEPT` にする（§3）。
- host に MPFR（libmpfr-dev）・gmpy2・mpmath・sollya を入れた（`sudo apt-get install libmpfr-dev libgmp-dev python3-mpmath
  python3-gmpy2 sollya`）。
