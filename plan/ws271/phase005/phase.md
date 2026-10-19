# ws271-p005 規約の全文の見直し（MIPS32 malta）

- 親: [WS271](../ws.md)。一緒に行う: [ws272-p004](../../ws272/phase004/phase.md)（MIPS64、共通の層は同じ file）。
- Queue: q1290 / q1290-i01（P1、worktree p1 / agent/p1、2026-10-19）。
- Status: in-progress（実装・build・host 試験は済み。T1 の回帰の結果を Q1 が判定するまで cleared にしない）。

## 範囲と受け入れ条件
WS271・WS272 が足した・変えた code（`src/hal/mips/`、`include/hal/arch/mips32.h`・`mips64.h` と `arch.h` の節、`platform/mips32-malta/`・`mips64-malta/`、kernel の elf・stat の枝、Makefile・menuconfig の追加、`plan/ws271/tests`・`plan/ws272/tests`）を [coding-style.md](../../coding-style.md) の全文と [Guardrail](../../guardrail.md) の出典の規則に照らし、書き方だけを直す（動作は変えない）。hal.h は不変。build warning 0（mips32・mips64）、host 試験 PASS、T1 の VFS の段の回帰。

## 直したこと（commit 166780446）
- 段落の comment と空行（§5・§10・§11）: `bsp-malta/board.c`・`irq.c`（登録の関数を段落に分けた）、`cache.c`、`clock.c`、`cmain.c`、`cpu.h`、`debug.c`、`fpu.c`、`int.c`（`finish_status` の前の空行と comment）、`page.c`、`selftest.c`、`space.c`（`hal_space_destroy` の critical section を段落に分けた）、`spin.h`、`task.c`、`task.h`（`private_data` の役の comment）、試験 2 つの成功の return。
- §6 の Boolean: `include/hal/arch/mips32.h`・`mips64.h` の `hal_atomic_uint_try_acquire` の `return previous == 0U;` を `if` と 2 つの return に。`static inline` を §1 の綴り `static __inline` に、`hal_atomic_relax(void)` の引数を別の行に。
- 出典（Guardrail「出典は該当箇所に block 単位で」）: file の先頭の comment にあった「Values referenced:」を使う block の前へ移した: `cache.c`（Config1 の field は define の block、cache 命令の op は各 loop の前）、`space.c`（`tlb_update` の前）、`page.c`（`PAGE_LOW_BYTES` の define の前）、`fpregs.S`（`cfc1`・`ctc1` の前）。`fpu.c` の先頭の行は値が全部 `asm.h` の block にすでに出典付きで在るので消した。
- 見て直さなかった物: `mips_irq_save()`〜`mips_irq_restore()` の critical section の本体の最初の行（style-check の paragraph-comment の 21 件）は §5 の critical section の形（取得・解放の前後の空行、comment は取得の上）で正しい。`io.h`・`space.c` の `return mips_segment_pointer(...)` は失敗の無い address の変換で §11 の「意味のある呼び出し」に当たらない。kernel の elf・stat の枝、`arch.h` の節、platform の Makefile・ld は周りと同じ形で直す所が無い。

## VFS initialization failed が 2 回出る件
- 原因: `src/kern/main.c` 218〜221 行で `kern_logf("VFS initialization failed (%d); entering idle.\n", error);` が同じ文で 2 回続けて書かれている（2026-10-03 の repo の作り直しの時点から、全 arch 共通）。HAL の出力の二重ではない。
- kernel の本体の振る舞いなので直さず記録だけ（範囲外）。Q1 へ: 片方を消すかは kernel の担当の判断。

## 確認（2026-10-19 P1、worktree p1）
- `make -B ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix -j16` → rc=0、`grep -ci warning` = 0（build/tmp/p005-m32.log）。
- `make -B ZEDBSD_CONFIG=plan/ws272/tests/config-mips64-malta.mk vmunix -j16` → rc=0、warning 0（build/tmp/p004-m64.log）。
- host: `plan/ws271/tests/pte-host-test.c`・`status-host-test.c` を `-m32 -DHAL_ARCH_MIPS32` と `-DHAL_ARCH_MIPS64`（`cc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal/mips`）→ 4 つとも PASS。
- `python3 plan/tools/style-check.py`（対象の C の file 全部）: 67 件 → 21 件（残りは上の critical section）。`git diff --check` 問題無し。
- QEMU: 未実施（自分で起動しない規則）。下の T1 の依頼。

## T1 への依頼（Q1 経由、ws272-p004 と同じ依頼 1 つ）
```
依頼: ws271-p005・ws272-p004（MIPS32・MIPS64 の規約の見直し、書き方だけ）の VFS の段の回帰（T1-733 と同じ）
image: worktree p1（agent/p1）の commit 166780446 以降を main に merge した後、
  make ZEDBSD_CONFIG=plan/ws272/tests/config-mips64-malta.mk vmunix   → build/mips64-malta/vmunix
  make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix   → build/mips32-malta/vmunix
起動 1（MIPS64）: BOOT_MODE=mips64-malta BOOT_MEMORY=256M BOOT_PATTERN='VFS initialization failed'
  plan/tools/boot-test.sh build/mips64-malta/vmunix
PASS 1: screendump の PNG に最後の 4 行が読める:
  MIPS64 HAL PAGING PASS refills=... invalids=... modifies=... generation=...
  MIPS64 HAL READY DISPLAY VGA PAGING PASS TIMER TICK
  MIPS64 CPU 1 ONLINE
  VFS initialization failed (...); entering idle.   （2 回出るのは kernel の main.c の既知の挙動、判定に影響しない）
起動 2（MIPS64、512M）: 起動 1 と同じで BOOT_MEMORY=512M。PASS 2: 起動 1 と同じ最後の 4 行。
起動 3（MIPS32）: BOOT_MODE=mips32-malta BOOT_MEMORY=256M BOOT_PATTERN='VFS initialization failed'
  plan/tools/boot-test.sh build/mips32-malta/vmunix
PASS 3: MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK、MIPS32 CPU 1 ONLINE、VFS initialization failed ...
FAIL の時: 画面の最後の行の PNG を返す（解析は P1、gdbstub の手順は ws272-p003 の依頼と同じ）。
```
