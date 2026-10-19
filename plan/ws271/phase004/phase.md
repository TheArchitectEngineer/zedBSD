<!-- awesome-plan project=zedbsd record=ws271-p004 -->

# ws271-p004 TLB・space・lazy FPU・atomic と self-test、kernel の link、VFS の段まで（MIPS32 mipsel、QEMU malta）

- 親: [WS271](../ws.md)、設計: [design.md](../design.md)（改訂 2）§2.3・§3.2〜§3.7・§6・§6.1・§7、Queue: q1283 / q1283-i01（P1、2026-10-19）
- Status: in-progress（実装・build・host の試験は済み。T1 の QEMU の画面の確認待ち。Q1 の判定まで cleared にしない）
- 前提: [p003](../phase003/phase.md)（T1-728 で試験中、2026-10-19 時点で結果は未着）。p004 は p003 の commit の上に積んだ。p003 が FAIL なら p004 の画面にも同じ所で出る。
- HAL の API: `include/hal/hal.h` は変えていない。`include/hal/arch/mips32.h` も変えていない（U1 の事前承認の形のまま、p002 で入った物）。

## 範囲と受け入れ条件
1. hal.h の task・irq・clock（rtc）・space・pmem・cpu・debug の API を MIPS32 で実装する。
2. kernel を link する（`platform/mips32-malta/standalone.c` を外し、kernel の source の一覧を足す。platform の binding は `src/kern/platform/hal-console.c`）。
3. TLB（refill、対の同期 M1、software の bit M2、ASID の世代 M3）、lazy FPU（FR=1、FCSR の消毒 M5）、atomic（enter の style）、cache（Config1 の形と色 M7）。
4. design.md §6.1 の self-test 10 項目が全部通った時だけ `PAGING PASS`。どれかが落ちたら `MIPS32 HAL PAGING FAIL <番号>` を出して止まる。
5. build warning 0、短い host 試験。到達点（T1 の画面）: `MIPS32 HAL PAGING PASS …`、`MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK`、`MIPS32 CPU 1 ONLINE`、`VFS initialization failed`。

## 実装（commit 33560ba12・97889be0b、worktree p1 / agent/p1、main 6bb830002 の上）
| file | 中身 |
| --- | --- |
| `src/hal/mips/vectors.S` | EBase+0x000 の TLB refill（k0・k1 だけ、0x80 byte ちょうど。refill の数を数え、`mips_tlb_root` の root → leaf の偶・奇の PTE を 6 bit 右へ shift して EntryLo0/1、`tlbwr`。root の entry が 0 なら無効の対） |
| `src/hal/mips/space.c`・`space.h`・`pte.h` | space の作成・破棄・switch・map・unmap・prot（`HAL_SPACE_NONE` を受ける）・query・clear_flags・flush・flush_range・map_device（KSEG1）。TLB Invalid・Modified の処理（`mips_space_fault`）。PTE を変える全部の操作で `tlbp`→`tlbwi`（書き直しか、KSEG0 の index ごとに違う VPN の無効の entry）。ASID は 1〜255、space ごとの (ASID, 世代)、使い切ったら世代を進めて TLB を全部無効に。self-test 用の counter・ASID の上限の変更・世代と ASID の読み出し |
| `src/hal/mips/page.c`・`page.h` | 物理 page の bitmap（ppc32 と同じ形）。RAM は物理 0 から board の memsize（最大 256 MiB）、image の終わりまでと initrd を予約。`hal_pmem_to_kernel` は KSEG0、`hal_pmem_map_uncached` は KSEG1（先に D cache の書き出し・無効化） |
| `src/hal/mips/cache.c`・`cache.h` | Config1 から I・D の line・way・way 数、way > 4 KiB なら色あり。`hal_dcache_*`（Hit 系の cache 命令）、`hal_sync_instruction_stream`（D の書き出し → I の無効化 → `sync` → `jr.hb`）、色ありの時の全部の色の Index の書き出し（map・unmap で呼ぶ） |
| `src/hal/mips/fpu.c`・`fpu.h`・`fpregs.S` | lazy FPU。Config1.FP と FIR.F64 の両方で使う（FR=1 を起動時に一度）。switch は context を名指して CU1 を落とすだけ、CpU（CE=1）で前の owner へ保存・今の context を復元。復元の `ctc1` の前に FCSR の Cause を消す。FPE の時の Cause の消去 |
| `src/hal/mips/task.c`・`task.h` | ppc32 の task の形（UP）: create（kernel・user）・init context・fork・exec・signal の入口と戻り・destroy・switch・tls・private・transfer・stack の guard と最深の記録。switch は space と FPU の context を替え、`mips_kernel_stack_pointer` を新しい task の stack の上端に |
| `src/hal/mips/int.c`・`trap.h` | 例外の dispatch: 割り込み（user なら frame を publish して kernel の戻りの処理）、TLB の 3 つ（space で直れば再実行）、CpU（FPU）、syscall（user → `kernel_syscall_handler`、番号 v0・引数 a0〜a3・t0・t1、EPC+4、戻り値 v0、a3=0）、それ以外は user・kernel の fault の handler へ。戻りの Status は毎回作る（user は `mips_user_status`、kernel は保存した値に今の IM と今の CU1）。self-test の fault の hook |
| `src/hal/mips/trap.S`・`dispatch.S` | `mips_user_frame_entry`（新しい user task の最初の戻り）。kernel task の入口で先に `mips_task_finish_switch` |
| `src/hal/mips/clock.c`・`clock.h`・`irq.c`・`irq.h` | kernel への tick（`mips_clock_start_kernel` の後だけ、ack は `MIPS_ACK_LOCAL`）、`hal_rtc_read_counter`（Count と wrap の 64 bit、160 MHz）、`hal_rtc_read_epoch_time` は無し。`hal_irq_*`（IE、EOI の記録。line の登録は ppc32 と同じく拒む） |
| `src/hal/mips/cpu.c`・`atomic.c`・`debug.c` | idle（`ei; wait; di`）・halt・barrier（`sync`）・mmio・port I/O の拒否・panic・UP の cpu の関数、atomic の enter/leave（割り込みを止める）、gpregs（Status・Cause・BadVAddr は書かない、M4）・fpregs（書く時に FCSR を消毒、M5）、single step と debug point は無し |
| `src/hal/mips/selftest.c`・`selftest.h` | design.md §6.1 の 10 項目（下） |
| `src/hal/mips/cmain.c` | cache・FPU・pmem・TLB の初期化 → 8259 → tick → 割り込みを止めて `kernel_entry`。`hal_cpu_start_others`（kernel の heap と初期 task の後に kernel が呼ぶ。ppc32・m68k と同じ所）で self-test と READY・CPU 1 ONLINE の行 |
| `src/hal/mips/bsp-malta/board.c`・`bsp.h` | memory の大きさ・initrd の物理の範囲・kernel の handoff（MBR の第 1 区画、riscv32 と同じ）・`hal_reset`（FPGA の SOFTRES に `0x42`）・`hal_poweroff`（止まる）・画面の有無 |
| `platform/mips32-malta/vmunix.mk`・`vmunix.ld` | kernel の source の一覧（riscv32 と同じ、fdt.c を除く）、`$(BUILD)/kernel/%.o` の規則、新しい HAL の file。`.hightext` を `.text` に |
| `src/kern/elf.c`・`include/kern/elf.h`・`include/uapi/stat.h` | `EM_MIPS`（8）、`HAL_ARCH_MIPS32`・`HAL_ARCH_MIPS64` の ELF の枝、o32 の stat の枝（design.md §5） |

### self-test（`MIPS32 HAL PAGING PASS` の条件、design.md §6.1）
1. map の後の store・load の値が一致し、refill が 1 回。
2. 偶だけ使った対（奇は V=0）に奇を map → store・load の値が一致し、TLB Invalid が 2 回以下。
3. map 直後 ACCESSED=0、load の後 1、clear_flags の後 0、再 load で 1。
4. load だけの page は DIRTY=0、store で 1、TLB Modified が 1 回。read-only の page への store は保護の fault（hook が数える）で page の値は不変。
5. `HAL_SPACE_NONE` の page の load は保護の fault、unmap の後の load は「無い」の fault（区別される）。
6. 2 つの space の同じ VA の違う page を A → B → A で読み分ける。
7. ASID の上限を 4 に下げ、5 つの space を 2 周 → 世代が進み、全部の値が正しい。上限は戻す。
8. 今でない space の flush で ASID が 0 になり、選ぶと新しい ASID で値が正しい。range の flush の後の load で refill が 1 回。destroy した space の ASID が同じ世代で次の space に配られない。
9. 2 つの context が `$f0`〜`$f31` と FCSR を lazy の経路で交互に入れて読み、各々の値が戻る。Cause と Enable の同じ bit（invalid、`0x00010800`）を立てた FCSR を入れても FPE が起こらず、Cause が消える。FPU を使えない CPU では `MIPS32 HAL PAGING FPU NONE`。
10. `mips_user_status(0xffffffff, owner でない, 使える)` に CU0・ERL・BEV・CU1 が無く、KSU=user・EXL・IE・IM=全部。

設計からの差分・注:
- PTE の bit 5 は design.md §3.4 では「予約」だが、READABLE（読んでよい）に使った（V の条件「prot に READ か EXEC」を software の bit で持つため）。WRITE だけの map も読める（MIPS は書けるなら読める）。
- self-test は `HAL_MIPS_SELFTEST` で切り替えず常に build する（`PAGING PASS` は通常の起動の判定の行、design.md §6）。試験だけの経路は ASID の上限の変更と kernel の fault の hook で、どちらも self-test の間だけ有効、終わったら戻す。
- self-test は kernel の heap が要る（`hal_space_create` が `kernel_alloc` を使う）ので、cmain ではなく `hal_cpu_start_others` で行う（ppc32・m68k が READY の行を出す所と同じ）。
- user の TLS（`hal_task_set_tls`）は task に持つだけ。user が読む方法（UserLocal か `rdhwr $29` の emulation）は userland の WS で決める。
- 外部割り込み（8259）の line を kernel に出す `hal_irq_register` は ppc32 と同じく拒む（使う driver が無い）。board の中の登録の関数は p003 のまま。
- 色の全部の書き出しは map と unmap の時だけ（copyout で既に map 済みの page に書いた時は無い）。QEMU の 24Kf は色が無いので no-op。制限は design.md §3.5 のとおり。
- `platform/mips32-malta/standalone.c` は build から外したが、file は残っている（削除は Q1 に依頼）。

## 確認（2026-10-19 P1）
- `make -B ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix -j16` → rc=0、`grep -ci warning` = 0（build/tmp/p004-b4.log、worktree p1）。cmain の修正の後の build も rc=0、`warning:` 0（p004-b7.log）。
- link 後の検査（vmunix.mk の中）: 未定義の symbol 無し、small data の section 無し、`$gp` の命令は trap frame の 2 つだけ。vmunix は 6.0 MiB の ELF、`.text` は `0x80100000` から。
- `llvm-objdump -d`: refill は `0x80101000`〜`0x8010107f`（32 命令、0x80 byte ちょうど、XTLB の `0x080` の手前）、偶の PTE の index は `(va >> 10) & 0xff8`、`srl 6` → EntryLo0/1 → `ehb` → `tlbwr` → `eret` を目で確かめた。
- `strings`: `MIPS32 HAL PAGING PASS …`・`MIPS32 HAL READY DISPLAY %s PAGING PASS TIMER %s`・`MIPS32 CPU 1 ONLINE`・`VFS initialization failed` が image にある。
- host の試験: [tests/pte-host-test.c](../tests/pte-host-test.c)（`cc -m32 -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal/mips -DHAL_ARCH_MIPS32 …`）→ `PASS`（V・D の規則、PROT_NONE、EntryLo の frame の位置）。[tests/status-host-test.c](../tests/status-host-test.c) → `PASS`。
- QEMU: **未実施**（自分で起動しない規則）。下の T1 の依頼。self-test の 10 項目は QEMU でしか確かめられない。

## T1 への依頼（Q1 経由）
```
依頼: ws271-p004（MIPS32 malta の TLB・space・FPU と self-test、kernel の link、VFS の段まで）の画面の確認
image: worktree p1（agent/p1）の commit 97889be0b を main に merge した後、
  make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix
  → build/mips32-malta/vmunix
起動: BOOT_MODE=mips32-malta BOOT_MEMORY=256M
  BOOT_PATTERN='VFS initialization failed'
  plan/tools/boot-test.sh build/mips32-malta/vmunix
  （login prompt は出ない。kernel が VFS の段で root が無いので止まる）
PASS: screendump の PNG に次の行が全部読める:
  MIPS32 HAL TRAP PASS
  MIPS32 HAL CONTEXT PASS
  MIPS32 HAL FPU F64 fir=…
  MIPS32 HAL TIMER TICK ticks=10 count=160000000 Hz
  MIPS32 HAL PAGING PASS refills=… invalids=… modifies=… generation=…
  MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK
  MIPS32 CPU 1 ONLINE
  VFS initialization failed (…); entering idle.
  （画面が流れて上の行が消えていてもよい。最後の 4 行は必須）
FAIL の時（画面の最後の行を PNG で返し、次を gdbstub で見てほしい）:
  qemu-system-mipsel -M malta -m 256M -vga std -display none -S -gdb tcp::<port> -kernel build/mips32-malta/vmunix
  gdb-multiarch build/mips32-malta/vmunix
    set architecture mips:isa32r2
    target remote :<port>
    break mips_early_exception ; break mips_halt_forever ; break hal_fatal ; continue
  - `MIPS32 HAL PAGING FAIL <n>`: その番号と、`mips_halt_forever` で止めた時の bt、`p mips_tlb_refill_count`、
    `p tlb_invalid_count`、`p tlb_modified_count`、`p asid_generation`、`p asid_next`。
  - `MIPS32 TRAP FAULT` か `fatal:` の行: その行と画面の r0〜r31、`hal_fatal` で止めた時の bt と `info registers`。
  - READY の行が出ずに止まる・固まる: Ctrl-C で止めて bt、`x/4i $pc`、`info registers`（status・cause・epc・badvaddr）、
    `p mips_tlb_root`、`p current_space`。
  - 画面が p003 の行（TIMER TICK）の前で止まる: p003（T1-728）と同じ所の問題。
  serial の log は判定に使わない。
```

## 残り
- T1 の結果（Q1 の判定）。FAIL なら uncleared にして再開する。
- Q1 に依頼: `platform/mips32-malta/standalone.c` の削除（build から外した、他から参照なし）。片付け: `build/tmp/p004-b1.log`〜`p004-b7.log`・`build/tmp/cmain.c.orig`・`build/tmp/pte-host-test`・`build/tmp/status-host-test`・`build/tmp/undef.txt`（worktree p1）。
- p005: 規約の全文の見直し（この Phase の新しい file を含む）。
- 実機（WS273 以降）: 色の page coloring、RI/XI、UserLocal、FPU の Unimplemented Operation、8259 の line を kernel に出す。
