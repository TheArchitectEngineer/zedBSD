<!-- awesome-plan project=zedbsd record=ws076 -->

# WS076: libc の libm を自前で正しく書き直す（src/libc）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計: 関数の一覧、精度の目標、方式、試験）から
<!-- awesome-plan-current:end -->

## 目標

2026-09-28 ユーザー:「libmは独自に書いてください。libcのツリーに入れてください。」

[BUG-078](../bugs/BUG-078.md) の libm（`src/libc/math.c`）の精度と正しさの問題を、既存の libm を取り込まずに自前の実装で直す。
置き場は libc の tree（`src/libc/`）。

## 完了の条件（案、p001 で確定）

- `pow`・`exp`・`log`・`sin`・`cos`・`tan` ほかの主な関数が誤差 1 ulp 以内（整数の冪など正確に表せる結果は正確）。`fmod`・`remainder`・`remquo`
  は正確（IEEE 754 のとおり）。`fma` は融合した結果。特殊な値（NaN・±Inf・±0・subnormal）と errno・例外の扱いが C 標準・POSIX のとおり。
- host の参照（glibc・MPFR 等の高精度の計算）との照合の試験（無作為の入力と境界の値）が通る。
- ブラウザの JS の `%`・Math.* が Chromium と一致（BUG-078 の operators.js の行）。
- 変更した source の規約の全文の確認。

## Phase の案

| Phase | 内容 | Status |
| --- | --- | --- |
| ws076-p001 | 設計: 対象の関数の一覧と今の実装の状態、精度の目標、方式（引数の縮約・多項式近似・表・double-double）、試験（host の高精度の参照、ulp の計測） | planning |
| ws076-p002 | 正確な関数: fmod・remainder・remquo・fma・frexp/ldexp 系・丸め系の確認 | planning |
| ws076-p003 | exp・exp2・expm1・log・log2・log10・log1p | planning |
| ws076-p004 | pow（整数の冪の正確な経路と一般の場合） | planning |
| ws076-p005 | 三角関数と逆三角関数（大きな引数の縮約）、双曲線関数 | planning |
| ws076-p006 | その他（cbrt・hypot・erf・erfc・lgamma・tgamma ほか）と float・long double の版 | planning |
| ws076-p007 | 規約の全文の照合と回帰 | planning |
