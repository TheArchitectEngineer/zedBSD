<!-- awesome-plan project=zedbsd record=ws271-p003 -->

# ws271-p003 例外・timer・8259・task（MIPS32 mipsel、QEMU malta）

- 親: [WS271](../ws.md)、設計: [design.md](../design.md)（改訂 2）§1.3・§2.3（M5）・§3.3（M4）・§3.6・§9、Queue: q1281 / q1281-i01（P1、2026-10-19）
- Status: in-progress（実装と build・host の試験は済み。T1 の QEMU の画面の確認待ち。Q1 の判定まで cleared にしない）
- 前提: [p002](../phase002/phase.md)（cleared、T1-725）。HAL の API（hal.h）は変えていない。arch の header（`include/hal/arch/mips32.h`）も変えていない。

## 範囲と受け入れ条件
1. general の例外の入口と出口（EBase+0x180 → `trap.S`）、38 word の trap frame（o32 の home 16 byte の上、計 168 byte）、ExcCode ごとの dispatch。
2. user に戻る Status は毎回組み立てる（M4）。保存した値は使わない。kernel への戻りは保存した値に今の IM。
3. FCSR の書き込みの前に Cause を消す関数（M5）。FPU の lazy の経路そのものは p004。
4. CP0 Count/Compare の 1000 Hz の tick（IP7）、Count の wrap を数えた 64 bit の counter。
5. PIIX4 の 8259 の対の初期化（全部 mask、cascade だけ開く）、OCW3 の poll と specific EOI、line の登録（level の時は ELCR）。
6. context switch（s0〜s7・fp・ra、Status は保存しない）と kernel task の最初の入口。
7. build warning 0。到達点（T1 の画面）: `MIPS32 HAL TRAP PASS`・`MIPS32 HAL CONTEXT PASS`・`MIPS32 MALTA PIC 8259 mask=fffb`・`MIPS32 HAL TIMER TICK ticks=10 count=160000000 Hz`・`MIPS32 HAL STOP AFTER TICK`。
   design.md §6 の最終の行（`MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK`）は p004 の到達点で、p003 は `TIMER TICK` を含む上の行を出して止まる。

## 実装（commit 1d9d4bfa2、worktree p1 / agent/p1）
| file | 中身 |
| --- | --- |
| `src/hal/mips/asm.h` | KSU の user、Cause の IP の mask、IP2（board）・IP7（timer）、ExcCode、FCSR の Cause の mask、trap frame の offset と大きさ、dispatch の word の数 |
| `src/hal/mips/vectors.S` | +0x180 を `mips_trap_entry` へ。refill・XTLB refill・cache error は p002 の early の報告のまま（TLB は p004） |
| `src/hal/mips/trap.S` | 入口: KSU を見て user なら `mips_kernel_stack_pointer` の stack へ、k0・k1 以外の全 GPR・hi・lo・EPC・Cause・BadVAddr・Status を保存、Status の KSU・EXL・IE を落として `mips_trap(frame)`。出口: `di` → frame の Status に EXL を立てて書く → EPC・hi・lo・GPR → sp を最後に → `eret` |
| `src/hal/mips/trap.h` | `struct mips_trap_frame`（`hal_gpregs` と同じ並び、offset を static assert） |
| `src/hal/mips/status.h` | `mips_user_status`（KSU=user・EXL・IE・今の IM、FPU の owner だけ CU1、FPU を使える時 FR。CU0・ERL・BEV は立たない）、`mips_kernel_status`、`mips_float_control_sanitize`（FCSR の bit 12〜17 を消す） |
| `src/hal/mips/int.c` | `mips_trap`: Int（IP7 → clock、IP2 → board）、Sys（self-test の番号だけ答える、v0・a3・EPC+4）、他は frame を画面に出して止まる。戻りの Status を `status.h` で作る。`mips_trap_selftest` |
| `src/hal/mips/clock.c`・`clock.h` | Count の rate を board から、間隔 = rate / 1000。Compare を書き直し、遅れたら now + 間隔。wrap を数えた 64 bit の counter（割り込みを止めて読む） |
| `src/hal/mips/dispatch.S` | `mips_task_dispatch`（12 word、s0〜s7・fp・ra）、`mips_kernel_task_entry`（`ei`、s0 の関数に s1 を渡す、戻ったら止まる） |
| `src/hal/mips/task.c`・`task.h` | `mips_context_selftest`（手で作った stack の task に 2 回 switch、Status が不変、kernel task の入口が引数を渡し IE だけを立てる）、`mips_task_returned` |
| `src/hal/mips/bsp-malta/irq.c` | 8259 の ICW1〜4、mask、poll（cascade なら slave も）、specific EOI、`mips_board_irq_register`（level なら ELCR の bit） |
| `src/hal/mips/bsp-malta/board.c`・`bsp.h`・`io.h` | `mips_board_count_frequency`（160 MHz）、`mips_io_read8` |
| `src/hal/mips/cmain.c` | console の後に trap・context の self-test、8259、clock、IM7・IM2 と IE を開け、Count で上限を付けて 10 tick を待つ（来なければ `MIPS32 HAL TIMER FAIL`）。止まる |
| `platform/mips32-malta/vmunix.mk`・`standalone.c` | 新しい file の一覧。`$gp` の検査は trap frame の `sw/lw $gp, …($sp)` を除外（その 2 命令だけが該当することを objdump で確かめた） |

設計からの差分・注:
- p003 の vmunix は p002 と同じく HAL だけ（kernel を link しない）。tick は数えるだけで `kernel_timer_handler` は呼ばない（kernel を link する p004 で配線）。hal.h の `hal_task_*`・`hal_irq_*`・`hal_rtc_read_counter` 等は p004 で、ここで作った内部の関数の上に置く。
- `task.S` の名は object の `task.o` が `task.c` と衝突するため `dispatch.S`（riscv32 と同じ名）。design.md §3.1 の表の `task.c / task.S` はこの名に読み替える。
- ELCR（`0x4d0`・`0x4d1`）の address は PIIX4 の datasheet の値（QEMU の i8259_common.c は取得していない）。今は登録する line が無いので経路は未実行。
- 出典（Guardrail の「MIT・GPL の出典のデータの引用」）: 値は block ごとの `Values referenced:`（MIPS32 PRA、QEMU v10.0.11 の malta.c・cpu.c・cpu-defs.c.inc・i8259.c、Intel 8259A・82371AB の datasheet）。QEMU の code・名前・構造は写していない。

## 確認（2026-10-19 P1）
- `make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix` → rc=0。`make -B …` の出力で `warning` の数 0（`grep -ci warning` = 0）。
- link 後の検査（vmunix.mk の中）: 未定義の symbol 無し、`.sdata` 等無し、`$gp` は trap frame の保存・復元の 2 命令だけ（`llvm-objdump -d | grep '$gp'` で 2 行を目で確かめた）。
- `llvm-objdump -d`: `mips_trap_entry`（EBase+0x1c0）の KSU の分岐と遅延 slot の `move $27,$sp`、frame の offset（`0x10($sp)` から）、`mips_trap_return` の `di`→Status（EXL）→EPC→hi/lo の順、self-test の `syscall` の前後（v0 に番号、a3 に 1、戻りで v0・a3 を比べる）を目で確かめた。
- host の試験: [tests/status-host-test.c](../tests/status-host-test.c)（`cc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal/mips …`）→ `PASS`。user の Status に CU0・ERL・BEV が立たない、KSU=user・EXL・IE・IM が今の値、CU1 は owner だけ、FR は使える時だけ、kernel の戻り、FCSR の Cause の消去。
- QEMU: **未実施**（自分で起動しない規則）。下の T1 の依頼。

## T1 への依頼（Q1 経由）
```
依頼: ws271-p003（MIPS32 malta の例外・timer・8259・task）の画面の確認
image: worktree p1（agent/p1）の commit 1d9d4bfa2 を main に merge した後、
  make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix
  → build/mips32-malta/vmunix
起動: BOOT_MODE=mips32-malta BOOT_MEMORY=256M
  BOOT_PATTERN='MIPS32 HAL STOP AFTER TICK'
  plan/tools/boot-test.sh build/mips32-malta/vmunix
  （login prompt は出ない。HAL が画面に行を出して止まる）
PASS: screendump の PNG に次の行が全部読める（p002 の 3 行の後に）:
  MIPS32 HAL TRAP PASS
  MIPS32 HAL CONTEXT PASS
  MIPS32 MALTA PIC 8259 mask=fffb
  MIPS32 HAL TIMER TICK ticks=10 count=160000000 Hz
  MIPS32 HAL STOP AFTER TICK
FAIL の時（画面の最後の行を PNG で返し、次を gdbstub で見てほしい）:
  qemu-system-mipsel -M malta -m 256M -vga std -display none -S -gdb tcp::<port> -kernel build/mips32-malta/vmunix
  gdb-multiarch build/mips32-malta/vmunix
    set architecture mips:isa32r2
    target remote :<port>
    break mips_trap ; break mips_early_exception ; break mips_halt_forever ; continue
  - `MIPS32 TRAP FAULT` か `EARLY EXCEPTION` が出た: その行と、mips_early_exception なら a0〜a3、mips_trap なら
    `p/x *(struct mips_trap_frame *)$a0` と bt。
  - `MIPS32 HAL TIMER FAIL ticks=0` が出た、または TICK の行が出ない: mips_trap を消し、`break mips_clock_interrupt` で
    来るかを見る。来なければ `info registers` の status（IM7=bit15・IE=bit0）・cause（IP7=bit15）、`p $count`・`p $compare`
    （gdb の名前が無ければ monitor の `info registers` の CP0 の欄）。
  - TRAP PASS の前で止まる: `break mips_trap_entry` で syscall が入口に来るか、`stepi` で eret まで追う。
  serial の log は判定に使わない。
```

## 残り
- T1 の結果（Q1 の判定）。FAIL なら uncleared にして再開する。
- p004: TLB・space・FPU（lazy、ここの `mips_float_control_sanitize` を `ctc1` の全部の場所で使う）・atomic・self-test（§6.1 の #10 は status.h の関数を target でも確かめる）、kernel の link、hal.h の task・irq・clock の API。
- 消す物: `build/tmp/p003-build.log`・`build/tmp/p003-buildB.log`・`build/tmp/status-host-test`（worktree p1、Q1 が消す）。
