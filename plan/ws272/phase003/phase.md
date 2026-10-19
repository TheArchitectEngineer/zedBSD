<!-- awesome-plan project=zedbsd record=ws272-p003 -->

# ws272-p003 MIPS64（mips64el）の例外・XTLB refill・3 段の table・atomic・M8、kernel の link（VFS の段まで）

- 親: [WS272](../ws.md)、設計: [WS271 の design.md](../../ws271/design.md) §3.3〜§3.7・§6.1・§8（M1〜M5・M8・M9）、Queue: q1287 / q1287-i01（P1、2026-10-19）、disposition: normal
- Status: in-progress（実装と build は済み、T1 の画面の確認待ち。Q1 の判定まで cleared にしない）
- 前提: ws272-p002（入口・console、T1-731 で試験中）、ws271-p003・p004（共通の層 `src/hal/mips/` の trap・TLB・space・FPU・self-test）。hal.h は不変。HAL の API の差分は無い（`include/hal/arch/mips64.h` は p002 のまま）。

## 範囲と受け入れ条件
1. `MIPS_HAL_CONSOLE_ONLY` と `platform/mips64-malta/standalone.c` を build から外し、mips32 と同じ HAL の全部と kernel を link する（`int64.c` と `atomic.c` は要らない: n64 は 64 bit の演算を持ち、atomic は native）。
2. 共通の層を幅で切り替える: trap frame は register の幅（n64 は 38 × 8 = 304 byte、home の領域無し）、dispatch は `REG_S`/`REG_L`、EPC・BadVAddr・EntryHi・EntryLo は `dmfc0`/`dmtc0`。
3. XTLB refill（EBase+0x080）が 3 段の table（9 + 9 + 9 + 12、PTE 64 bit、`dsrl 6`）を歩く。512 GiB 以上の miss は general の例外へ（fault）。
4. Status: KX・UX を入口で立て、user に戻る Status にも入れる（FR は p002 からの FPU の初期化で立つ）。
5. M8: 正の物理の範囲 = low の alias（物理 0〜min(RAM, 256 MiB)）と、RAM > 256 MiB の時の物理 `0x80000000 + 256 MiB`〜`0x80000000 + RAM`。image と ENVP・ramdisk は low の側で予約。direct map は XKPHYS。
6. build warning 0（mips64・mips32 の両方）。mips32-malta の挙動を壊さない。
7. 到達点（T1 の画面）: 下の依頼。

## 実装（commit 81e42c1c1、worktree p1 / agent/p1）
| file | 中身 |
| --- | --- |
| `src/hal/mips/asm.h` | 幅の節に `MIPS_REGISTER_BYTES`・`REG_S`/`REG_L`・`PTR_SRL`・`MFC0_WIDE`/`MTC0_WIDE`（C の inline asm 用に `MIPS_WIDE_MFC0_STRING`・`MIPS_WIDE_MTC0_STRING`）。`MIPS_STATUS_KERNEL_ADDRESSING` は 64 bit で KX\|UX（`0xa0`）。table の節（32: 2 段 1024 word、64: 3 段 512 doubleword、`MIPS_TABLE_LEVELS`・`MIPS_TABLE_INDEX_BITS`・`MIPS_TABLE_ENTRY_SHIFT`・`MIPS_ROOT_SHIFT` 30・`MIPS_MIDDLE_SHIFT` 21）、`MIPS_PTE_FRAME_MASK`（64: PABITS 36）、`MIPS_ENTRY_HI_VPN2_MASK`（64: SEGBITS 42）、`MIPS_USER_LIMIT`（64: 512 GiB、`MIPS_USER_SHIFT` 39）。frame の offset は register の幅の倍数、`MIPS_ARGUMENT_HOME_SIZE` は n64 で 0 |
| `src/hal/mips/trap.S`・`dispatch.S` | 保存・復元を `REG_S`/`REG_L`、EPC・BadVAddr を `MFC0_WIDE`/`MTC0_WIDE`、stack の加算を `PTR_ADDIU`、kernel stack の読みを `LOAD_ADDRESS`＋`REG_L` |
| `src/hal/mips/vectors.S` | 64 bit: +0x000 は early の報告、+0x080 は `.text` の `mips_xtlb_refill`（0x80 byte に収まらないので j）。refill: 数を数え、`BadVAddr >> 39` が 0 でなければ `mips_trap_entry` へ、root → middle → leaf を `ld` で歩き、空なら空の対、偶・奇の PTE を `dsrl 6` して `dmtc0` EntryLo0/1、`tlbwr`。32 bit の refill は不変。`MIPS_HAL_CONSOLE_ONLY` の枝を削除 |
| `src/hal/mips/trap.h`・`int.c`・`task.c`・`debug.c` | frame の member と `mips_kernel_stack_pointer` を `uintptr_t`。system call の戻り値・fork の子の値・signal の番号・戻り値の読みで 32 bit への切り詰めを外す。TRAP FAULT の表示を `%08llx` |
| `src/hal/mips/pte.h`・`space.c`・`space.h` | `mips_pte_t`（`uintptr_t`）。`walk_leaf` を段数の loop に（`table_below` が中間・葉の table を作る）。root・`mips_tlb_root`・`system_root` は `uintptr_t`。EntryHi・EntryLo の読み書きは wide、無効の entry の EntryHi は `mips_segment_pointer(KSEG0 + index × 8 KiB)`（CKSEG0）。clear_flags の mask を `~(mips_pte_t)`（64 bit で上の bit を落とさない）。device の窓は 64 bit で XKPHYS uncached（512 MiB の制限なし） |
| `src/hal/mips/io.h` | `mips_cached_address()`・`mips_uncached_address()`（32: KSEG0/KSEG1、64: XKPHYS）、`mips_image_physical()` |
| `src/hal/mips/page.c`・`page.h`・`bsp-malta/board.c`・`bsp.h` | M8: page の番号 i は RAM の先頭からの数、i < low_pages は物理 i × 4 KiB、それ以上は `0x80000000 + i × 4 KiB`。block は 2 つの alias を跨がない。`max_paddr` を番号に直す。`page_of()` は low の alias の page の high の alias（`0x80000000`〜+256 MiB）を「無い」とする。bitmap は 64 bit で 2 GiB 分。board は環境の `ememsize` を読み `mips_board_memory_total()` で返す（無ければ low と同じ）。未使用の `mips_ram_base`・`mips_ram_end` を削除。MEMORY の行は `low= high= total= free=` |
| `src/hal/mips/cache.c` | 色の flush の Index の address を `mips_segment_pointer()`（64 bit で `0x80000000` が xuseg になっていた）。`instruction_hazard` の `la` を `%hi`/`%lo` の 2 命令に（64 bit の「la used to load 64-bit address」） |
| `src/hal/mips/status.h` | user の Status に `MIPS_STATUS_KERNEL_ADDRESSING`（32 bit は 0） |
| `src/hal/mips/cmain.c` | `MIPS_HAL_CONSOLE_ONLY` の枝を削除。64 bit は console の後に XKPHYS の確認（`MIPS64 XKPHYS PASS`）をしてから trap の self-test へ |
| `platform/mips64-malta/vmunix.mk` | mips32 と同じ HAL・kernel の source（`atomic.c`・`int64.c` 無し）、`-DKERN_USER_ABI_MIPS64`、`-mlong-calls`（下）、LTO に `+long-calls` |

設計からの差分・見つけた事（Q1 の確認を求める）:
- **LLVM の不具合の回避（`-mlong-calls`）**: n64＋`+sym32`＋`-mno-abicalls` で、compiler が作る libcall（構造体の copy の `memcpy` 等）が `jalr memcpy`（symbol の jalr）になり、object の出力で `error: expected an immediate` になる（`-S` の text は assemble できる。scratchpad の t.c で再現、`-mips-jalr-reloc=false` では消えない）。`-mlong-calls` で call は `lui %hi`/`daddiu %lo`/`jalr $25` になり通る。call ごとに 2 命令増える。toolchain は変えていない。
- mips32 の挙動の変化: MEMORY の行の書式だけ（`base= total=` → `low= high= total=`）。trap・table・refill の命令は同じ（root の index に `& 1023` が入ったが 32 bit の値の `>> 22` には同じ）。
- `platform/mips64-malta/standalone.c` は build から外した。削除は Q1 に依頼（rm をしない規則）。

## 確認（2026-10-19 P1）
- `make -B ZEDBSD_CONFIG=plan/ws272/tests/config-mips64-malta.mk vmunix -j8`（worktree p1、`build/mips64-malta`）→ rc=0、出力の `warning` 0。link の後の検査（未定義 0、small data 無し、`$gp` の命令無し）を通った。
- `make -B ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix -j8` → rc=0、`warning` 0。
- `llvm-objdump -d`（64 bit）: `mips_vectors+0x80` が `j mips_xtlb_refill`。refill は `dsrl32 …,7`（>>39）→ `ld` の 3 段（`dsrl 0x1b`・`0x12`・`0x9`、`andi 0xff8`・`0xff8`・`0xff0`）→ `dmtc0 $2`・`$3` → `tlbwr`。`mips_trap_entry` は `daddiu $sp,-0x130`（304）・`sd`・`dmfc0 $14`/`$8`・`dmtc0 $14`。
- host: `plan/ws271/tests/pte-host-test.c`（`-m32 -DHAL_ARCH_MIPS32` と `-DHAL_ARCH_MIPS64` の両方）→ PASS、`status-host-test.c`（既定と `-DHAL_ARCH_MIPS64`）→ PASS。
- QEMU: **未実施**（自分で起動しない規則）。下の T1 の依頼。共通の file の変更は mips32 の回帰も同じ依頼に入れる。

## T1 への依頼（Q1 経由）
```
依頼: ws272-p003（MIPS64 malta の例外・XTLB refill・3 段の table・atomic・M8、kernel の link、VFS の段まで）
      と、共通の層の変更による ws271（MIPS32 malta）の回帰
image: worktree p1（agent/p1）の commit 81e42c1c1 以降を main に merge した後、
  make ZEDBSD_CONFIG=plan/ws272/tests/config-mips64-malta.mk vmunix   → build/mips64-malta/vmunix
  make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix   → build/mips32-malta/vmunix
起動 1（MIPS64）: BOOT_MODE=mips64-malta BOOT_MEMORY=256M BOOT_PATTERN='VFS initialization failed'
  plan/tools/boot-test.sh build/mips64-malta/vmunix
PASS 1: screendump の PNG に次の行が読める（上の行は流れて消えていてもよい。最後の 4 行は必須）:
  MIPS64 HAL ENTRY prid=... status=000000a0 ebase=80101000 config=...
  MIPS64 XKPHYS PASS physical=00101000 word=...
  MIPS64 HAL TRAP PASS
  MIPS64 HAL CONTEXT PASS
  MIPS64 HAL FPU F64 fir=...
  MIPS64 HAL TIMER TICK ticks=10 count=160000000 Hz
  MIPS64 HAL PAGING PASS refills=... invalids=... modifies=... generation=...
  MIPS64 HAL READY DISPLAY VGA PAGING PASS TIMER TICK
  MIPS64 CPU 1 ONLINE
  VFS initialization failed (...); entering idle.
起動 2（MIPS64、M8 の high の alias）: 起動 1 と同じで BOOT_MEMORY=512M
PASS 2: 起動 1 と同じ最後の 4 行。MEMORY の行が読めれば `low=256 MiB high=256 MiB total=512 MiB` であること
  （流れて消えていれば未確認と書く）。
起動 3（MIPS32 の回帰）: BOOT_MODE=mips32-malta BOOT_MEMORY=256M BOOT_PATTERN='VFS initialization failed'
  plan/tools/boot-test.sh build/mips32-malta/vmunix
PASS 3: ws271-p004 と同じ最後の 4 行（MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK、MIPS32 CPU 1 ONLINE、
  VFS initialization failed ...）。
FAIL の時（画面の最後の行を PNG で返し、gdbstub で次を見てほしい）:
  qemu-system-mips64el -M malta -cpu MIPS64R2-generic -m 256M -vga std -display none -S -gdb tcp::<port> -kernel build/mips64-malta/vmunix
  gdb-multiarch build/mips64-malta/vmunix
    set architecture mips:isa64r2
    target remote :<port>
    break mips_early_exception ; break mips_halt_forever ; break hal_fatal ; continue
  - `MIPS64 EARLY EXCEPTION`: その行（code・epc・badvaddr）、a0〜a3 と bt。
  - `MIPS64 HAL PAGING FAIL <n>`: 番号と bt、`p mips_tlb_refill_count`、`p tlb_invalid_count`、`p tlb_modified_count`、
    `p/x mips_tlb_root`、`p asid_generation`。
  - `MIPS64 TRAP FAULT` か `fatal:`: その行と r0〜r31、`hal_fatal` での bt と `info registers`。
  - 固まる: Ctrl-C で bt、`x/4i $pc`、`info registers`（status・cause・epc・badvaddr）。
  serial の log は判定に使わない。
```

## 残り
- T1 の結果（Q1 の判定）。FAIL なら uncleared にして再開する。
- Q1 に依頼: `platform/mips64-malta/standalone.c` の削除（build から外した、他から参照なし）。片付け（worktree p1 の build/ の中の自分の一時物）: `build/tmp-m64.log`・`build/tmp-m32.log`・`build/tmp-n/`・`build/tmp-host/`（p002 の残り: `build/tmp-m32-before`・`build/tmp-m32-before.dis`・`build/mips32-malta/vmunix.dis`・`build/tmp-ro-tmp-m32-before`・`build/tmp-ro-vmunix`）。
- `-mlong-calls` の回避: LLVM の sym32 の libcall の不具合が直れば外す（toolchain の判断は Q1）。
- 規約の全文の見直し（WS272 の最後の Phase）。
