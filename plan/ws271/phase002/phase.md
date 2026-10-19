<!-- awesome-plan project=zedbsd record=ws271-p002 -->

# ws271-p002 入口と console の画面（MIPS32 mipsel、QEMU malta）

- 親: [WS271](../ws.md)、設計: [design.md](../design.md)（改訂 2）§1.5・§1.6・§3.1・§3.3・§4・§5・§7.1、Queue: q1279 / q1279-i01（P1、2026-10-19）
- Status: in-progress → 実装と build を終えた（2026-10-19 15:02 P1、commit 4a5d4885f）。QEMU の画面の確認は T1 の依頼（下）。clearance は T1 の結果を Q1 が判定してから。
- 前提: 共有の LLVM は zedbsd10（`clang --print-targets` に mipsel、Q1）、boot-test.sh の `mips32-malta` の mode（main 652af9697、Q1）。HAL の API の差分は U1「同じ形で事前承認」（Guardrail の 2026-10-19 の行）。

## 範囲と受け入れ条件
1. 共通の層 `src/hal/mips/` の入口: `_start` の最初の命令で EBase を image の vector に向け、`ehb` の後 Status を 0（BEV=0・ERL=0・EXL=0・IE=0・KSU=kernel・CU0=0・CU1=0）、Config.K0 を KSEG1 から 3 に、bss、boot stack、`mips_cmain(a0..a3)`。
2. bsp-malta: GT-64120 の PCI の config（bus 0、function 0）、std VGA（1234:1111）の BAR0・BAR2 を PCI0 の memory 窓 0 に割り当て Memory Space を立て、DISPI で 640x480x32、`0x3c0` に `0x20`。console は 80x30 の 8x16 font。
3. build の配線: `platform/mips32-malta/`、Makefile・menuconfig、試験の config [tests/config-mips32-malta.mk](../tests/config-mips32-malta.mk)。build の warning 0。
4. HAL の差分は事前承認の形だけ: `include/hal/arch.h` の 2 節、`include/hal/arch/mips32.h`、`boot.command-line`（`hal_get_arch_handoff`）。hal.h は不変。
5. 到達点（T1 の画面）: `MIPS32 HAL ENTRY`・`MIPS32 MALTA memory=`・`MIPS32 DISPLAY VGA 640x480x32`・`MIPS32 HAL STOP AFTER CONSOLE` の行が画面に出る。

## 実装（commit 4a5d4885f、worktree p1 / agent/p1）
| file | 中身 |
| --- | --- |
| `include/hal/arch.h` | `HAL_ARCH_MIPS64`・`HAL_ARCH_MIPS32` の節（proposed/hal-mips.md D1 のまま。mips64.h は WS272 の p002 で作る） |
| `include/hal/arch/mips32.h` | D2 のまま（tick 1000 Hz、signal frame の head 16 byte、ENTER の atomic と ll/sc の try_acquire、relax は `nop`、`hal_gpregs`・`hal_fpregs`・`hal_vregs`、debug の値、static assert） |
| `src/hal/mips/asm.h` | CP0 の番号・Status・Cause・Config の bit、KSEG0/1、vector の offset、stack の大きさ（独自の名前、block ごとに `Values referenced:` で PRA） |
| `src/hal/mips/locore.S` | 入口（範囲 1） |
| `src/hal/mips/vectors.S` | EBase の 4 KiB の vector（+0x000・+0x080・+0x100・+0x180）。p003 までは全部が専用の stack で `mips_early_exception()` へ（Cause・EPC・BadVAddr・Status を画面に出して止まる） |
| `src/hal/mips/cmain.c`・`cpu.h` | `mips_cmain`（表の写し → 画面 → `MIPS32 HAL ENTRY prid status ebase config` → board の記述 → 止まる）、`mips_early_exception`、`mips_halt_forever`、CP0 の読み出し |
| `src/hal/mips/lib.c` | riscv32 の lib.c と同じ hal_* の C runtime（SBI を外し board の画面だけ） |
| `src/hal/mips/io.h`・`spin.h` | KSEG1 の device access、`di`/`ei` の irq save/restore と HAL の spin lock |
| `src/hal/mips/bsp-malta/board.c` | argv・環境の表の写し（KSEG0 の low memory の中だけ読む）、`rd_start=`・`rd_size=` の除去、`memsize`、`hal_get_arch_handoff("boot.command-line")`（無い時 NULL）、画面と console の配線 |
| `src/hal/mips/bsp-malta/pci.c`・`pci.h` | GT の `0x1be00cf8`・`0xcfc`（mipsel では素の 32 bit。gt64120.c の `bswap()` は LE の CPU で MByteSwap が立つので入れ替えない、P1 が source を読んで確かめた） |
| `src/hal/mips/bsp-malta/framebuffer.c`・`.h` | std VGA（BAR の大きさを all-ones で測り、BAR0 = `0x10000000`、BAR2 = BAR0 + VRAM の大きさ、窓 128 MiB に収まるかを確かめる） |
| `src/hal/mips/bsp-malta/cons.c`・`cons.h`・`font.c`・`font.h` | riscv32 の bsp-virt の console と font の写し（80x30 に） |
| `platform/mips32-malta/` | Makefile・vmunix.mk・vmunix.ld（KSEG0 `0x80100000`、`.text.boot` の後に 4 KiB 揃えの `.text.vectors`）・run.mk・standalone.c |
| `Makefile`・`tools/menuconfig.py` | `mips32-malta` の variant・target、ASFLAGS・CPPFLAGS の mips32、menuconfig の platform・board・variant、`NO_DESKTOP_ARCHITECTURES` に mips32（mips64-malta は platform の dir が無いので WS272 の p002 で足す） |

設計からの差分（Q1 の確認を求める）:
- **p002 の vmunix は HAL だけ**（kernel の object を link しない）。kernel の link には hal.h の全部の関数が要り、例外・TLB・task は p003・p004 の範囲のため。HAL が読む kernel の symbol `kernel_putc` だけを `platform/mips32-malta/standalone.c` が NULL で持つ。kernel を link する Phase（p004 の見込み）で standalone.c を外し、riscv32 と同じ kernel の source の一覧を足す。
- LTO の code 生成は linker の中で走り `-mno-abicalls`・`-G 0` を見ないので（ld.lld が「linking abicalls code with non-abicalls code」を警告した）、link に `-mllvm -mattr=+noabicalls -mllvm -mips-ssection-threshold=0` を渡した（design.md §2.2 の未検証の点の答え）。link の後に `.sdata`・`.sbss`・`.lit4`・`.lit8` が無いこと、`$gp` を使う命令が無いことを確かめる（`.got` は lld が予約の 2 word を常に置くので対象外）。
- Config.K0 を 3 にする時、cache の初期化は QEMU（cache を模さない）を前提にして行わない。実機（WS273）では K0 を変える前に cache の初期化が要る。
- `boot.command-line` は `-append` が無い（argv[1] が空）時 NULL。

出典（Guardrail の「MIT・GPL の出典のデータの引用」）: 値は block ごとの複数行の `Values referenced:`（QEMU v10.0.11 の malta.c・gt64120.c・vga-pci.c、sun4u と同じ v10.0.0 の vga-pci.c・bochs-vbe.h の offset と index、Linux 6.8.12 の bochs.c の `0x20`、PCI Local Bus Specification 3.0、MIPS32 PRA）。code・名前・構造は写していない。PRA の文書は取得していない（値は広く知られた architecture の定義、design.md §0 の * のまま）。vga-pci.c の `PCI_VGA_*_OFFSET` と pci_ids.h の 1234:1111 は v10.0.11 の header を取得していないので、既存の sun4u の port と同じ v10.0.0 の出典の値を使った。

## 確認（2026-10-19 P1）
- `make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix`（worktree p1、BUILD は `build/mips32-malta`）→ rc=0。`make -B …` の出力で `warning` の数 0。
- `llvm-readelf -h -l`: Entry `0x80100000`、Flags `noreorder, o32, mips32r2`（abicalls・pic 無し）、LOAD は全部 KSEG0。
- `llvm-nm -n`: `_start` 0x80100000、`mips_vectors` 0x80101000（4 KiB 揃い）、bss 0x80105000〜0x8010b3f0。
- `llvm-objdump -d`: `_start` の最初の 4 命令が `lui/addiu $8, mips_vectors` → `mtc0 $8, $15, 1` → `ehb` → `mtc0 $zero, $12`。KSEG1 への `jr`、Config の K0、`jr.hb` で KSEG0 へ戻る、bss の loop、`jal mips_cmain` の遅延 slot の `move $7` を目で確かめた。`$gp` を使う命令 0。
- host の試験: C の関数の単体の試験は無し（board.c の解析は KSEG0 の address を読むので host で動かせない）。上の ELF の検査を host の確認とした。
- QEMU: **未実施**（自分で起動しない規則）。下の T1 の依頼。

## T1 への依頼（Q1 経由）
```
依頼: ws271-p002（MIPS32 malta の入口と console）の画面の確認
image: worktree p1（agent/p1）の commit 4a5d4885f を main に merge した後、
  make ZEDBSD_CONFIG=plan/ws271/tests/config-mips32-malta.mk vmunix
  → build/mips32-malta/vmunix
起動: BOOT_MODE=mips32-malta BOOT_MEMORY=256M
  BOOT_PATTERN='MIPS32 HAL STOP AFTER CONSOLE'
  plan/tools/boot-test.sh build/mips32-malta/vmunix
  （login prompt は出ない。HAL が画面に 4 行出して止まる）
PASS: screendump の PNG に次の行が全部読める:
  MIPS32 HAL ENTRY prid=... status=... ebase=80101000 config=...
  MIPS32 MALTA memory=262144 KiB ramdisk=00000000+0
  MIPS32 DISPLAY VGA 640x480x32 pci=0:<n> fb=10000000 mmio=11000000
  MIPS32 HAL STOP AFTER CONSOLE
  （status は 0、config の下 3 bit は 3 の見込み。pci の n は 18 の見込み）
FAIL の時: 画面が黒い・何も出ない・cirrus の画面のままなら、gdbstub で止めて見てほしい:
  qemu-system-mipsel -M malta -m 256M -vga std -display none -S -gdb tcp::<port> -kernel build/mips32-malta/vmunix
  gdb-multiarch build/mips32-malta/vmunix
    set architecture mips:isa32r2
    target remote :<port>
    break mips_cmain ; break mips_early_exception ; break malta_framebuffer_init ; continue
  mips_early_exception に止まったら a0〜a3（cause・epc・badvaddr・status）と bt、
  malta_framebuffer_init を finish した戻り値（v0）、monitor の info pci・info mtree の PCI の窓の部分。
  画面は PNG を返す。serial の log は判定に使わない。
```

## 残り
- T1 の結果（Q1 の判定）。FAIL なら p002 を uncleared にして再開する。
- p003: 例外・timer・8259・task（vectors.S の early の経路を trap の入口に置き換える）。
- kernel の link と standalone.c の削除（p004 の見込み、上の差分）。
- 消す物: 無し。
