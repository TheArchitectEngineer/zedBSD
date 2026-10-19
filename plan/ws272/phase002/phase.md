<!-- awesome-plan project=zedbsd record=ws272-p002 -->

# ws272-p002 MIPS64（mips64el）の入口と console の画面（QEMU malta）

- 親: [WS272](../ws.md)、設計: [WS271 の design.md](../../ws271/design.md) §3.1・§7.1・§8、Queue: q1286 / q1286-i01（P1、2026-10-19）、disposition: normal
- Status: in-progress（実装と build は済み、T1 の画面の確認待ち。Q1 の判定まで cleared にしない）
- 前提: 共有の LLVM の Mips（`mips64el-unknown-elf`）、boot-test.sh の `mips64-malta` の mode（main にある）。HAL の差分は U1「同じ形で事前承認」（Guardrail 2026-10-19 の行）: 新しい `include/hal/arch/mips64.h`（D3）。`arch.h` の節（D1）は ws271-p002 で入済み。hal.h は不変。

## 範囲と受け入れ条件
1. 共通の層 `src/hal/mips/` を `HAL_ARCH_MIPS64` で build する。幅の差は `asm.h` の macro（`MIPS_HAL_NAME`・`PTR_ADDU`・`PTR_ADDIU`・`LOAD_ADDRESS`・`MIPS_STATUS_KERNEL_ADDRESSING`・`MIPS_XKPHYS_*`）と `io.h` の `mips_segment_pointer()`（32 bit の KSEG の値を符号拡張）で吸収し、`#if` は asm.h の幅の節と下の段の切り替えだけ。
2. 入口: EBase を最初に、Status は KX=1（その他 0）、Config.K0=3（CKSEG1 から）、bss、stack、`mips_cmain`。CKSEG0 `0xffffffff80100000` に link した ELF64。
3. XKPHYS: image の 1 word を CKSEG0・XKPHYS cached（`0x9800…`）・uncached（`0x9000…`）で読み比べ、画面に `MIPS64 XKPHYS PASS` を出す。
4. bsp-malta の PCI・VGA・console を mips64 で build（source は共通のまま）。
5. build の配線: `platform/mips64-malta/`、Makefile・menuconfig に `mips64-malta`、試験の config [tests/config-mips64-malta.mk](../tests/config-mips64-malta.mk)。warning 0。mips32-malta の build も warning 0 で、挙動が変わらない。
6. 到達点（T1 の画面）: 下の依頼の 5 行。

## 実装（commit 974ee1728、worktree p1 / agent/p1）
| file | 中身 |
| --- | --- |
| `include/hal/arch/mips64.h` | D3: tick 1000 Hz、signal frame の head 16 byte（riscv64 と同じ形）、NATIVE の atomic、ll/sc の try_acquire、relax は `nop`、`hal_gpregs` は 64 bit、`hal_fpregs` は mips32 と同じ、LP64 の static assert |
| `src/hal/mips/asm.h` | 幅の節（上の macro）、`LOAD_ADDRESS`（`lui %hi`＋`addiu %lo`。両方の幅で同じ） |
| `src/hal/mips/io.h` | `mips_segment_pointer()`、`mips_device_address()` がそれを使う |
| `src/hal/mips/locore.S` | `la` → `LOAD_ADDRESS`、Status に `MIPS_STATUS_KERNEL_ADDRESSING`、pointer の加算は `PTR_ADDU`・`PTR_ADDIU` |
| `src/hal/mips/vectors.S` | `MIPS_HAL_CONSOLE_ONLY` の時 refill と general も `mips_early_vector` へ（trap・table が無い段）。early の stack は `LOAD_ADDRESS`・`PTR_ADDIU` |
| `src/hal/mips/cmain.c` | `MIPS_HAL_CONSOLE_ONLY` の時、board の記述の後に（64 bit なら）XKPHYS の確認、`HAL STOP AFTER CONSOLE` で止まる。trap 以降と `hal_cpu_start_others`・`wait_for_ticks` はその段では build しない |
| `src/hal/mips/*.c`・`bsp-malta/*.c` | 画面の行の頭の `"MIPS32 "` を `MIPS_HAL_NAME " "` に（文字列の連結、mips32 では同じ文字列） |
| `src/hal/mips/bsp-malta/board.c` | 表の pointer を `mips_segment_pointer()` で（64 bit で `0x80002000` を符号拡張） |
| `platform/mips64-malta/` | Makefile・vmunix.mk（HAL の 8 C file＋2 S、`-DMIPS_HAL_CONSOLE_ONLY`）・vmunix.ld（`0xffffffff80100000`）・run.mk（`-cpu MIPS64R2-generic`）・standalone.c（`kernel_putc`、kernel を link する段で外す） |
| `Makefile`・`tools/menuconfig.py` | `mips64-malta` の variant・target、ASFLAGS・`-DHAL_ARCH_MIPS64`、menuconfig の platform・variant、`NO_DESKTOP_ARCHITECTURES` に mips64 |

設計からの差分（Q1 の確認を求める）:
- **clang に `-msym32` は無い**（`clang: error: unknown argument: '-msym32'`）。code 生成には `-Xclang -target-feature -Xclang +sym32` で渡り、小さな object で `R_MIPS_HI16`/`LO16` の 2 命令になり `ld.lld -m elf64ltsmip` が `0xffffffff8010…` に link できた（scratchpad の t.S・b.c、2026-10-19）。統合 assembler は `.set sym32` を受けず `la` を 64 bit の 6 命令にする（「la used to load 64-bit address」の警告）ので、.S は `LOAD_ADDRESS` を使う。LTO の link には `-mllvm -mattr=+noabicalls,+sym32` を渡す。
- p002 の vmunix は HAL だけ（ws271-p002 と同じ段の形）。段の切り替えは build の define `MIPS_HAL_CONSOLE_ONLY`（mips64-malta の vmunix.mk だけが立てる）。p003 で trap・XTLB・table を入れる時に外す。
- 正の物理の範囲（§8 M8）は、この段では memory の allocator が無いので code にしていない（p003 の page.c の 64 bit 化で入れる）。XKPHYS の確認は image の物理（low の alias の側）で行う。
- Status は KX だけ（UX・FR は p003、user と FPU の段）。

## 確認（2026-10-19 P1）
- `make -B ZEDBSD_CONFIG=plan/ws272/tests/config-mips64-malta.mk vmunix`（worktree p1、`build/mips64-malta`）→ rc=0、出力の `warning` 0。link の後の検査（未定義 0、small data 無し、`$gp` の命令無し）を通った。
- `llvm-readelf -h -l`: ELF64、Entry `0xffffffff80100000`、Flags `noreorder, mips64r2`（abicalls・pic 無し）、LOAD は全部 CKSEG0。
- `llvm-nm -n`: `_start` `0xffffffff80100000`、`mips_vectors` `0xffffffff80101000`、bss `…80105000`〜`…8010b3f0`。
- `llvm-objdump -d`: `_start` は `lui/addiu $8` → `mtc0 $8,$15,1` → `ehb` → `li $8,0x80` → `mtc0 $8,$12`。CKSEG1 へは `daddu`（`0xffffffff80100044 + 0x20000000`）、bss の loop は `daddiu`、XKPHYS の基底は `dsll32 …,0x1b`/`0x1c`（`0x98…`・`0x90…`）で組まれている。
- mips32: `make -B ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix` → rc=0、`warning` 0。前の vmunix（同じ worktree の merge 直後の build）と disassembly を比べ、差は `_start` の `mtc0 $zero,$12` が `li $8,0`＋`mtc0 $8,$12` になった 1 命令だけ（88 行、全部 `_start` の 4 KiB の中、vectors 以降の address は不変）。`.rodata` は byte 一致（`MIPS_HAL_NAME` の連結で文字列は同じ）。
- `make menuconfig-host-test` → MAC-T001 PASS。
- QEMU: **未実施**（自分で起動しない規則）。下の T1 の依頼。

## T1 への依頼（Q1 経由）
```
依頼: ws272-p002（MIPS64 malta の入口と console）の画面の確認
image: worktree p1（agent/p1）の commit 974ee1728 以降を main に merge した後、
  make ZEDBSD_CONFIG=plan/ws272/tests/config-mips64-malta.mk vmunix
  → build/mips64-malta/vmunix
起動: BOOT_MODE=mips64-malta BOOT_MEMORY=256M
  BOOT_PATTERN='MIPS64 HAL STOP AFTER CONSOLE'
  plan/tools/boot-test.sh build/mips64-malta/vmunix
  （login prompt は出ない。HAL が画面に 5 行出して止まる）
PASS: screendump の PNG に次の行が全部読める:
  MIPS64 HAL ENTRY prid=... status=00000080 ebase=80101000 config=...
  MIPS64 MALTA memory=262144 KiB ramdisk=00000000+0
  MIPS64 DISPLAY VGA 640x480x32 pci=0:<n> fb=10000000 mmio=11000000
  MIPS64 XKPHYS PASS physical=00101000 word=...
  MIPS64 HAL STOP AFTER CONSOLE
  （config の下 3 bit は 3 の見込み。pci の n は 18 の見込み）
FAIL の時: 画面が黒い・何も出ない・cirrus の画面のままなら、gdbstub で止めて見てほしい:
  qemu-system-mips64el -M malta -cpu MIPS64R2-generic -m 256M -vga std -display none -S -gdb tcp::<port> -kernel build/mips64-malta/vmunix
  gdb-multiarch build/mips64-malta/vmunix
    set architecture mips:isa64r2
    target remote :<port>
    （p001 の未確認 1・2: entry で info registers の a0〜a3・sp・pc、monitor の xp /4wx 0x2000）
    break mips_cmain ; break mips_early_exception ; break malta_framebuffer_init ; continue
  mips_early_exception に止まったら a0〜a3（cause・epc・badvaddr・status）と bt。
  画面は PNG を返す。serial の log は判定に使わない。
```

## 残り
- T1 の結果（Q1 の判定）。FAIL なら uncleared にして再開する。
- p003: `MIPS_HAL_CONSOLE_ONLY` を外し、trap（frame 304 byte、`REG_S`/`REG_L`）・XTLB refill・3 段の table・native atomic・正の物理の範囲・self-test、kernel の link と standalone.c の削除。
- 消す物（Q1 に依頼、worktree p1 の build/ の中の自分の一時物）: `build/tmp-m64.log`、`build/tmp-m32.log`、`build/tmp-m32-before`、`build/tmp-m32-before.dis`、`build/mips32-malta/vmunix.dis`、`build/tmp-ro-tmp-m32-before`、`build/tmp-ro-vmunix`。
