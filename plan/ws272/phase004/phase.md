# ws272-p004 規約の全文の見直し（MIPS64 malta）

- 親: [WS272](../ws.md)。[ws271-p005](../../ws271/phase005/phase.md) と一緒に行った（共通の層 `src/hal/mips/` は同じ file）。
- Queue: q1290 / q1290-i01（P1、worktree p1 / agent/p1、2026-10-19）。
- Status: in-progress（実装・build・host 試験は済み。T1 の回帰の結果を Q1 が判定するまで cleared にしない）。

## 範囲と受け入れ条件
WS272 が足した・変えた code（`include/hal/arch/mips64.h`、`platform/mips64-malta/`、共通の層の 64 bit の枝、kernel の elf の MIPS64 の枝、`plan/ws272/tests`）を coding-style.md の全文と Guardrail の出典の規則に照らして書き方だけを直す。hal.h は不変。

## 結果
- 直した内容・直さなかった物・VFS の 2 回の出力の原因（`src/kern/main.c` の同じ `kern_logf` が 2 回、kernel の本体、記録だけ）は [ws271-p005](../../ws271/phase005/phase.md) に書いた（同じ commit 166780446）。MIPS64 に固有の物: `include/hal/arch/mips64.h` の `hal_atomic_uint_try_acquire` の Boolean と `__inline`、`hal_atomic_relax` の引数の行。`platform/mips64-malta/`（Makefile・run.mk・vmunix.ld・vmunix.mk）と `plan/ws272/tests/config-mips64-malta.mk` は直す所が無い。
- 確認: mips64-malta・mips32-malta の `make -B … vmunix` は rc=0・warning 0。host の pte・status の試験（32・64 bit）PASS。QEMU は未実施。
- T1 への依頼: ws271-p005 の依頼 1 つで MIPS32・MIPS64 の両方を流す（T1-733 と同じ形）。
