<!-- awesome-plan project=zedbsd record=ws076 -->

# WS076: libc の libm を自前で正しく書き直す（src/libc）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p007（規約の全文の照合、math_errhandling、全表、ブラウザの operators.js、boot test）から
<!-- awesome-plan-current:end -->

## 目標

2026-09-28 ユーザー:「libmは独自に書いてください。libcのツリーに入れてください。」

[BUG-078](../bugs/BUG-078.md) の libm（`src/libc/math.c`）の精度と正しさの問題を、既存の libm を取り込まずに自前の実装で直す。
置き場は libc の tree（`src/libc/`）。

## 完了の条件（p001 で確定、[design.md](design.md) §2）

- 群 B（exp・exp2・expm1・log・log2・log10・log1p・pow・三角・逆三角・双曲線・逆双曲線・cbrt・hypot）が誤差 1 ulp 以内
  （double-double で求めて 1 回丸める。正確に表せる結果は正確）、群 C（erf・erfc・tgamma・lgamma）が 1 ulp 以内。sqrt は正しく丸める。`fmod`・`remainder`・`remquo`
  は正確（IEEE 754 のとおり）。`fma` は融合した結果。特殊な値（NaN・±Inf・±0・subnormal）と errno・例外の扱いが C 標準・POSIX のとおり。
- host の参照（glibc・MPFR 等の高精度の計算）との照合の試験（無作為の入力と境界の値）が通る。
- ブラウザの JS の `%`・Math.* が Chromium と一致（BUG-078 の operators.js の行）。
- 変更した source の規約の全文の確認。

## Phase の案

| Phase | 内容 | Status |
| --- | --- | --- |
| [ws076-p001](phase001/phase.md) | 設計: 対象の関数の一覧と今の実装の状態、精度の目標、方式（引数の縮約・多項式近似・表・double-double）、試験（host の高精度の参照、ulp の計測）→ [design.md](design.md) | cleared |
| [ws076-p002](phase002/phase.md) | `src/libc/math/` への分割と build、共通の header、正確な関数: sqrt・fma・fmod・remainder・remquo・frexp/ldexp 系・丸め系、試験の道具（MPFR の参照、runner、host） | cleared |
| [ws076-p003](phase003/phase.md) | exp・exp2・expm1・log・log2・log10・log1p | cleared |
| [ws076-p004](phase004/phase.md) | pow（整数の冪の正確な経路と一般の場合） | cleared |
| [ws076-p005](phase005/phase.md) | 三角関数と逆三角関数（大きな引数の縮約）、双曲線関数 | cleared |
| [ws076-p006](phase006/phase.md) | その他（cbrt・hypot・erf・erfc・lgamma・tgamma ほか）と float・long double の版 | cleared |
| ws076-p007 | 規約の全文の照合と回帰 | planned |
