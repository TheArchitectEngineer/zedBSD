<!-- awesome-plan project=zedbsd record=ws271-p001-design -->

# WS271・WS272 の設計: MIPS32（mipsel）・MIPS64（mips64el）の QEMU malta（framebuffer の console・VFS の段まで）

2026-10-19 P1（q1273、[ws271-p001](phase001/phase.md)）。親: [WS271](ws.md)（mipsel）・[WS272](../ws272/ws.md)（mips64el）。
手本は [WS263](../ws263/design.md)（riscv32）・[WS264](../ws264/design.md)（ppc32）・[WS265](../ws265/design.md)・[WS266](../ws266/design.md)。
§1〜§6 は MIPS32・MIPS64 の共通、§7 が WS271 に固有、§8 が WS272 の差分。WS272 の p001 は §8 の確認だけで済む形にした。

**改訂 2（2026-10-19 P1、q1277）**: [design-review-1.md](design-review-1.md)（Blocker 0・Major 9・Minor 14）の全項目を当てた。対応の一覧は末尾の §12。
主な変更: §3.4 の TLB の対の同期・software の bit・ASID の世代（M1〜M3）、§3.3 の user の Status（M4）、§2.3 の FPU（M5・M6）、§3.5 の cache の色（M7）、§8 の物理の範囲（M8）、§6.1 の self-test（M9）、§9 の Phase の分割（Minor 13）。

## 0. 出典と確認の水準

- QEMU の source。host の QEMU は Debian の 10.0.11。取得は全部 upstream の tag `v10.0.11`（`https://gitlab.com/qemu-project/qemu/-/raw/v10.0.11/<path>`）。置き場は `build/tmp/ws271-docs/`（worktree p1、git に入れない、file 名は path の `/` を `_` にした物）。
  版 1 の「（v10.0.3）」の注は、v10.0.3 との照合の記録が無いので外した。comment の `Value(s) referenced:` の版は「QEMU v10.0.11」に揃える。

  | path | SHA-256（写しの値。upstream との再照合は未確認） |
  | --- | --- |
  | hw/mips/malta.c | b5066d887fa1218c9deb140c6ac71d79b6c957915f9829646f67c7c35b1d4830 |
  | hw/mips/bootloader.c | d14c0f39609a9e4b55172344b04402066576a9adda6f7c00d2cd47108c5a791b |
  | hw/pci-host/gt64120.c | d01b13236f0fc28b34115c68d59f8777ee7bed130780387c380f72f239800994 |
  | hw/isa/piix.c | 20f9adccce3f270695b4315444cd2a80f60704224becb933acf6fbfe975d67d9 |
  | hw/char/serial.c | 46548454bc48e12b430795fc69cb19f0349bbef3a63ee37c23aa365713978b91 |
  | target/mips/cpu-defs.c.inc | a29998857ec9c02aa11c7ab3d305510f49fb8e12d9051ab25a8a08fb31b5afcd |
  | target/mips/cpu.c | 04122500bffacbf6973483a2f58b0490dba594008fd9d1a02a8dd3129899d9a6 |
  | target/mips/system/cp0_timer.c | 8633c575aae479aeee6c9009aef579ad1304b9c6d8c6637a5b6b0bd1e4f22592 |
  | hw/display/vga-pci.c | 9c497f5f7ea5fd0a72399c4b242b58f32edee391907c941e2f40b9e0c719f03e |
  | system/vl.c | d065734e676f24075e28e4a0f4339bf8c5f7f7bec1b30d44c5de96063a761f15 |
  | hw/intc/i8259.c | 8ea7ab1cce169d826d82b41149f838fa4f50444ba98bc86874176a0bf1c0522e |

  後の 5 つの SHA-256 は 2026-10-19 に P1 が `sha256sum` で写しから取った（q1277）。
  QEMU は GPL-2.0。値（address・register の offset・bit）は事実として使い、code・名前・構造は写さない（[Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」、comment は block ごとの複数行の `Value(s) referenced:`）。
- MIPS の公開の資料: MIPS32/MIPS64 Architecture for Programmers Vol. III（Privileged Resource Architecture、PRA）、MIPS32 24K の Software User's Manual、Malta の User's Manual、GT-64120 の datasheet。**この Phase では文書を取得していない**（network の取得は QEMU の source だけ）。PRA の事実（KSEG の配置、vector の offset、EntryLo の形、hazard）は広く知られた architecture の定義として書き、p002 の着手で PRA の版を phase.md に記録する（未確認の印 *）。
- 「未確認」は source でも資料でも確かめていない事。p002 で gdbstub（`-S -gdb`）と monitor（`info registers`・`info mtree`・`info pci`・`xp`）で確かめる（gdb の操作は T1 に頼む）。

## 1. QEMU malta の事実

### 1.1 machine と CPU
- `qemu-system-mipsel -M malta`（malta が既定の machine）。CPU の既定は 32 bit の build で `24Kf`、64 bit の build で `20Kc`（malta.c `mips_malta_machine_init`）。
- `24Kf`（cpu-defs.c.inc 247〜259 行）: `CPU_MIPS32R2 | ASE_MIPS16`、FPU あり（Config1.FP、FCR0 に F64・L・W、つまり FR=1 が使える）、TLB 16 entry（Config1.MMU = 15）、`CCRes = 2`、SEGBITS 32・PABITS 32、Config3.VInt = 0（vectored interrupt 無し）、Config3.RXI = 0（RI/XI 無し）。cache は I・D とも 64 set × 16 byte × 2 way（IS=0・IL=3・IA=1、way は 1 KiB）。`SYNCI_Step = 32`。
- CPU の clock は 320 MHz（`mips_malta_instance_init` の `cpu-refclk`）。CP0 Count は clock を CCRes で割った速さ（cpu.c `mips_cp0_period_set`）→ **Count は 160 MHz**。HAL は board の値として 160 MHz を持つ。実機では board ごとの値か、別の clock（RTC の秒の境界）で測る（実機の WS で決める）。
- SMP: `-smp` > 1 は CPS を持つ CPU（I6400・P5600 等）でだけ CPS で作られ、24Kf では CPU が複数作られても割り込みは最初の CPU だけ（`create_cpu_without_cps`）。**UP で作る**。
- memory は最大 2 GiB。RAM の全体を物理 `0x80000000` に置き、先頭の `min(RAM, 256 MiB)` を物理 0 に alias、512 MiB を超える分を物理 `0x20000000` に alias（malta.c 1117〜1136 行）。既定の RAM の大きさは machine に指定が無く QEMU の既定（128 MiB、未確認*）。試験は `-m 256M`。

### 1.2 物理の memory map（32 bit の物理）
| 物理 | 中身 | 出典 |
| --- | --- | --- |
| `0x00000000`〜`0x0fffffff` | RAM の先頭 256 MiB の alias | malta.c |
| `0x00002000` | `-kernel` の時の PROM の引数と環境の表（16 entry × (4 byte の pointer ＋ 256 byte)） | malta.c `ENVP_PADDR` |
| `0x10000000`〜`0x17ffffff` | PCI0 の memory 窓 0（128 MiB） | malta.c 650〜664 行の値、gt64120.c 323〜389 行の式 |
| `0x18000000`〜`0x181fffff` | PCI0 の I/O 窓（ISA の port n は物理 `0x18000000 + n`） | 同上 |
| `0x18200000`〜`0x1bdfffff` | PCI0 の memory 窓 1（60 MiB） | 同上 |
| `0x1be00000` | GT-64120 の register（reset の `0x14000000` から bootloader が移す） | malta.c 640〜642 行 |
| `0x1e000000` | flash（4 MiB） | malta.c `FLASH_ADDRESS` |
| `0x1f000000` | FPGA（LED・ASCII の表示・reset の register 等）、`+0x500` が SOFTRES（`0x42` を書くと reset、malta.c 500〜502 行）、`+0x900` に CBUS UART（16550、reg shift 3、`serial_hd(2)`、CPU の IP4） | malta.c `malta_fpga_init` |
| `0x1fc00000` | reset の vector（flash の写し、read-only。`-kernel` の時は QEMU が書いた小さな bootloader） | malta.c `RESET_ADDRESS` |
| `0x80000000`〜 | RAM の全体 | malta.c |

窓の数値は、bootloader が書く LD・HD（malta.c 650〜664 行）を GT の解釈（`(HD + 1 - (LD & 0x7f)) << 21`）に当てて計算した（design-review-1 の Minor 1、P1 が 2026-10-19 に malta.c の行を読み直した）。PCI の bus address と CPU の物理 address は同じ。`info mtree` の確認は p002 で行う（確認の印は外す）。

### 1.3 PCI（GT-64120）と PIIX4
- config 空間は GT の `0x1be00000 + 0xcf8`（address）・`+ 0xcfc`（data）、PC と同じ形の type 1 の address。mipsel では普通の 32 bit の store でよい（gt64120.c は data に `pci_host_data_le_ops`）。
- GT の他の register は「bus の endian は常に逆」（gt64120.c `gt64120_writel` は GT_CPU の bit 12 が 0 なら byte を入れ替える）。HAL は GT の他の register に触れない。8259 の割り込みの番号は OCW3 の poll で取り、GT の IACK（`0xc34`）は使わない（§3.3）。
- device: slot 0 は GT 自身（vendor 11ab）。走査では読み飛ばす。PIIX4 は bus 0 の device 10（function 0 ISA・1 IDE・2 USB・3 PM）、AMD 79C973（pcnet）は slot 11。自動で置く device は slot 18 から（gt64120.c 1203〜1206 行の `pci_root_bus_new` の devfn_min が `PCI_DEVFN(18, 0)`）なので、VGA は slot 18 の見込み。HAL は bus 0 を走査する。
- PCI の割り込み: PIIX4 の PIRQ A〜D を ISA IRQ 10・10・11・11 に向ける（bootloader が PIIX の PIRQC に書く）。slot ごとの pin の写像は malta.c `malta_pci_slot_get_pirq`。
- 割り込み controller: PIIX4 の中の 8259 の対（ISA port `0x20`・`0xa0`、物理 `0x18000020`・`0x180000a0`）が CPU の IP2（Cause の bit 10、hw 0）に入る。CBUS UART は IP4、CP0 の timer は **IP7**（cpu.c 317 行の IntCtl の reset 値 `0xe0000000` で IPTI = 7）。
- **firmware（YAMON）は `-kernel` の時は走らない**。PCI の BAR の割り当て・command register の enable は誰もしない → HAL が自分で割り当てる（§1.5）。

### 1.4 UART
- CBUS UART（`0x1f000900`、16550、register の間隔 8 byte）と、Super I/O（SMS FDC37M817、ISA の上、COM1 = port `0x3f8` = 物理 `0x180003f8`）。
- serial は判定に使わない（AGENTS.md）。boot-test の対話の serial は `-serial` の 1 番目が Super I/O の COM1 と読んだ（未確認*: isa-superio の source を取得していない）。

### 1.5 画面（PCI の std VGA、Bochs の linear framebuffer）
- 既定の VGA は machine に `default_display` が無いので cirrus（system/vl.c `get_default_vga_model`）。**boot-test は `-vga std` を付ける**（device `VGA`、1234:1111）。
- std VGA（vga-pci.c）: BAR0 = VRAM（prefetchable memory、既定 16 MiB）、BAR2 = MMIO 4 KiB（`+0x400` VGA の I/O port の写し（port − 0x3c0）、`+0x500` Bochs の DISPI register（index i を `+0x500 + 2i` の 16 bit、little endian）、`+0x600` QEMU の拡張（byte order））。offset の値は vga-pci.c の `PCI_VGA_*_OFFSET`（header 側の定義、数値は*）。
- HAL の手順（bsp-malta の `framebuffer.c`）。手本は ramfb の riscv32 ではなく、std VGA を同じ手順で設定する `src/hal/sparcv9/bsp-sun4u/framebuffer.c` 41〜55 行（属性 controller の `0x20` の書き込みの前例）:
  1. bus 0 の device 1〜31 を走査して 1234:1111 を見つける（slot 0 の GT は飛ばす）。
  2. BAR0 に `0x10000000`、BAR2 に `0x11000000` を書き（大きさは all-ones の probe で確かめる。どちらも MEM0 の窓に収まる）、command に Memory Space を立てる。
  3. DISPI: ENABLE=0、XRES=640、YRES=480、BPP=32、ENABLE = ENABLED | LFB_ENABLED（0x41）。
  4. 属性 controller の index port（`0x3c0`、BAR2 `+0x400`）に `0x20`（palette の access を終えて画面を出す。sun4u の前例と同じ）。
  5. framebuffer は物理 `0x10000000` を **KSEG1（uncached、`0xb0000000`）** から描く。
  - console は 640x480 XRGB8888（U5）、8x16 の font で 80x30。font・cons は既存の bsp の物（riscv32 の bsp-virt の cons/font）を写す。
- `-display none` の時の screendump は graphic の console（VGA）を取り、FPGA の `vc:320x200` の text console を取らない見込み（QEMU が graphic の console を先に置く処理による、ui/console.c を取得していないので未確認*）。p002 の最初の T1 の画面で確かめる。

### 1.6 `-kernel` の起動と受け渡し
- QEMU は ELF を `cpu_mips_kseg0_to_phys` で物理に置く（KSEG0 に link した image の物理 = virt − 0x80000000）。entry が USEG（< 0x80000000）なら拒む（malta.c `load_kernel`）。
- 小さな bootloader が reset の vector（`0xbfc00000` から実行）に書かれ、GT を移し PCI の窓と PIRQ を設定してから kernel の entry へ跳ぶ（`bl_gen_jump_kernel`）:

  | register | 値 |
  | --- | --- |
  | sp | `ENVP_VADDR − 64` = `0x80001fc0` |
  | a0 | 2（引数の数） |
  | a1 | `ENVP_VADDR` = `0x80002000`（argv の表: [0] kernel の file 名、[1] `-append` の文字列（initrd があれば先頭に `rd_start=0x… rd_size=… `）） |
  | a2 | `ENVP_VADDR + 8`（環境の表: 名前と値の文字列の pointer が交互、NULL で終わる。`memsize`（low の RAM の byte 数）、`ememsize`（RAM の全体）、`modetty0`、`rngseed`） |
  | a3 | low の RAM の大きさ（`min(RAM, 256 MiB)`） |

  pointer は全部 KSEG0 の 32 bit（64 bit の build では符号拡張、§8）。
- **入口の CP0 の状態（source で確認）**: reset で Status = BEV | ERL（cpu.c 306 行）、`-kernel` の時は `main_cpu_reset` が ERL を消す（malta.c 1024〜1026 行）→ 入口は BEV=1・ERL=0・EXL=0・IE=0・KSU=kernel・CU0=0・CU1=0・FR=0。Config.K0 = 2（uncached、cpu-defs.c.inc 24〜25 行の `MIPS_CONFIG0`）。EBase の reset 値は `0x80000000`（cpu.c 300 行、`KSEG0_BASE | cpu_index`）。
- HAL は入口で Status を確定した値に書く: ERL=0、EXL=0、KSU=kernel、IE=0、**CU0=0**（kernel mode は CU0 無しで CP0 を使える。M4）、CU1=0、FR=1（FPU を使える時だけ、§2.3）、BEV=0 は EBase の設定の後（§7.1）。
- command line（Minor 5）: argv[1] の先頭の `rd_start=…`・`rd_size=…` の 2 語を HAL が解いて取り除き、残りを `hal_get_arch_handoff("boot.command-line")`（ppc32 と同じ名前、無い時 NULL）で kernel に渡す。initrd がある時は、その物理の範囲（`rd_start` は KSEG0 の virt なので `− 0x80000000` で物理に直す）を memory map から除く（allocator が上書きしないため）。initrd の中身を kernel に渡す handoff は今回は作らない（使う側が無い、§4 D4）。

## 2. toolchain（共通）

### 2.1 LLVM（Q1 が適用、ユーザーの許可 2026-10-19）
- 今の状態（2026-10-19、Minor 14）: 共有の `build/llvm` は zedbsd9 で、`--print-targets` に mips は出ない（design-reviewer が確かめた）。main の作業ツリーでは Q1 が `toolchain/llvm/llvm.mk`（targets に `Mips`）と `version.mk`（`zedbsd10`）を変え、共有の LLVM を作り直している（q1277 の時点、未 commit）。
- 案（[proposed/llvm-mips.md](proposed/llvm-mips.md)、Q1 が適用、patch level を `zedbsd9` → `zedbsd10`）:
  1. llvm.mk の targets に `Mips` を足す。
  2. `0001-add-zedbsd-x86-target.patch` と同じ形で、`clang/lib/Basic/Targets.cpp` の mips・mipsel・mips64・mips64el の switch に `case llvm::Triple::ZedBSD: ZedBSDTargetInfo<MipsTargetInfo>`（`__ZEDBSD__` を定義）、`ToolChains/ZedBSD.cpp` の linker の emulation: mipsel o32 → `elf32ltsmip`、mips64el n64 → `elf64ltsmip`（big endian は `elf32btsmip`・`elf64btsmip`、今回は作らない）。
  3. triple: `mipsel-unknown-zedbsd`（o32）・`mips64el-unknown-zedbsd`（n64、`-mabi=64` を既定に）。
- **kernel の build**: riscv32・ppc32 と同じく `--target=mipsel-unknown-elf -D__ZEDBSD__=1`（zedbsd の triple は userland が要る時まで要らない）。kernel の build に要るのは Mips の target だけ。
- U2（2026-10-19 ユーザー）: 「LLVM の Mips を待つ」。GCC の代わりの道は使わない。

### 2.2 kernel の flags（mipsel）
`--target=mipsel-unknown-elf -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls -fno-pic -G 0 -mno-gpopt -ffreestanding -D__ZEDBSD__=1`、
`ld.lld -m elf32ltsmip`、link は KSEG0 の `0x80100000`（§7.1）。kernel は浮動小数点を使わないので常に `-msoft-float`。
64 bit の除算・shift の helper は riscv32 と同じく `src/libc/int64.c` を LTO 無しで入れる。

toolchain の未検証の点（Minor 6、p002 の最初に小さな object で確かめる）:
- FPU の保存・復元の asm は `.set hardfloat` に加えて `.set fp=64`（FR=1 の奇数の 64 bit の register の `sdc1`・`ldc1`）と、o32 では `.set oddspreg` が要る見込み。LLVM の integrated assembler が受けるかを `llvm-mc` で確かめる。受けなければ `.word` で命令を組む。
- kernel は LTO が既定（Makefile 130 行）。`-G 0` は function の属性ではないので LTO で落ちる恐れがある。`-mno-gpopt` と合わせ、link の後に `readelf -S` で `.sdata`・`.sbss`・`.got` が無いことを確かめる（あれば p002 で LTO の flag を足すか、HAL を LTO から外す）。
- `-msym32`（MIPS64、§8）の clang の受け入れは未確認。無くても n64 の static の code は動く（symbol の address を 64 bit で組むだけ）ので、受けた時だけ使う。

### 2.3 浮動小数点（2026-10-19 ユーザー「まずはhardfloatでOKです。」「softfloatをオプションで選べるようにしたいですね。」、U3「FPXX（FR=1）」）
- 既定 hardfloat: user は o32 の `-mhard-float -mfpxx`（FR=0・FR=1 のどちらでも動く code）。HAL は FPU を使える時は常に Status.FR=1 で 32 個の 64 bit の FPR と FCSR を保存する（FP32 の code（FR=0 専用）は扱わない）。
- **FPU を使える条件（M6）**: 起動時に Config1.FP = 1 で、かつ FCR0（FIR、`cfc1 $0`）の F64（bit 22）= 1 の時だけ。どちらかが 0 なら FPU を使わない。その時は CU1 を立てず、user の FPU の命令は Coprocessor Unusable → SIGILL（soft の build と同じ扱い）、hardfloat の ELF の exec は userland の WS で拒む。QEMU の 24Kf・MIPS64R2-generic は両方 1（cpu-defs.c.inc）。**RG350 の JZ4770 が F64 を持つかは未確認で、WS273 の設計でもう一度確かめる**（U3 の前提。F64 が無ければ U3 を見直す判断が要る）。
- lazy FPU: CU1 を 0 にしておき、Coprocessor Unusable（CE=1）で有効にして復元する。owner の task を percpu に持ち、switch では CU1 を落とすだけ（保存は次の CpU の時に前の owner へ）。
- **FCSR の消毒（M5）**: FCSR を `ctc1` で書く全部の場所（lazy の復元、sigreturn、ptrace の `hal_fpregs` の書き込み、exec の初期化）で、先に Cause の bit（17〜12）を 0 にする。Cause と Enable の同じ bit が立った値を書くと、その `ctc1` で FPE が起こり、kernel の中で panic するため。FPE の例外（ExcCode 15）を user で受けた時は、FCSR を読んで SIGFPE を送り、保存する FCSR の Cause を消す。
- Unimplemented Operation（Cause.E、実機の 24Kf の denormal 等）は SIGFPE にする（emulator は作らない、制限）。FCSR.FS（flush to zero）は既定で 0 のまま（結果の値が変わるため既定にしない）。QEMU は E を起こさない。
- softfloat の option の置き場所（今回は作らない、決めておくだけ）:
  - config の変数 `ZEDBSD_FLOAT_ABI := hard | soft`（`tools/menuconfig.py` の mips の platform の時だけ出す、既定 hard）。Makefile は userland と libc の CFLAGS に `-mhard-float -mfpxx` か `-msoft-float` を足す。kernel は不変（常に soft）。
  - 既存の `src/libc/softfloat.mk`（Makefile 754・760 行、整数だけの compiler の runtime）がある（Minor 7）。soft の浮動小数点の helper（`__adddf3` 等）をこれで賄うか、compiler-rt の mips の builtins を足すか（[proposed/llvm-mips.md](proposed/llvm-mips.md) §3）は softfloat の WS で決める。hardfloat の o32 の userland にも 64 bit の除算の helper（`__divdi3` 等）が要り、これは既存の int64.c か softfloat.mk の整数の部分で賄う。
  - sysroot は ABI ごとに別の directory（`build/<platform>/sysroot` の名前に `-softfloat` を足す。toolchain の `sysroot.mk` の変更は Q1）。
  - ELF の ABI の flag（`EF_MIPS_*`、`.MIPS.abiflags` の fp_abi）で kernel の exec が hard/soft の不一致を拒む（userland の WS で）。

## 3. HAL の構成（共通の層 `src/hal/mips/`）

### 3.1 directory
```
src/hal/mips/            MIPS32・MIPS64 の共通（register 幅は asm.h の macro で切り替え）
  asm.h                  REG_S/REG_L/PTR_ADDU/LONGSIZE、CP0 の register の番号と bit（独自の名前）、hazard の macro
  locore.S               _start（EBase を最初に、§7.1）、Status の確定、bss、stack、C へ
  vectors.S              EBase+0x000 TLB refill、+0x080 XTLB refill（64）、+0x100 cache error、+0x180 general
  trap.S                 general の入口・出口（frame の保存・復元、user の Status の組み立て、eret）
  tlb32.S / tlb64.S      refill の handler（§3.4）、tlb の index・probe・write の小さな関数
  int.c                  Cause.ExcCode ごとの dispatch（Int・Mod・TLBL・TLBS・AdEL・AdES・Sys・Bp・RI・CpU・Ov・Tr・FPE）
  tlb.c                  ASID の割り当てと世代、flush、TLB の対の同期（§3.4）
  space.c / page.c       page table（§3.4）、hal_space_*
  cache.c                Config1 から line の大きさ・set・way、色（§3.5）
  task.c / task.S        context switch（s0〜s7・fp・sp・ra。Status は保存しない、§3.3）、新しい task の入口
  fpu.c / fpu.S          lazy FPU（§2.3）
  clock.c                CP0 Count/Compare の tick（§3.6）
  atomic.c / spin.h      ll/sc
  selftest.c             p004 の self-test（§6.1）
  percpu.c / lib.c / debug.c / cmain.c
src/hal/mips/bsp-malta/  board.c（handoff・環境の表・reset/poweroff）、pci.c（GT の config）、framebuffer.c、cons.c、font.c、
                         irq.c（PIIX4 の 8259）、uart.c（debug だけ）
```
共通にする理由: WS272 は同じ file を `HAL_ARCH_MIPS64` で build し、差分は refill（`tlb64.S`）・address の配置（§8）・`asm.h` の幅だけ。RG350（WS273）は `bsp-jz4770/` を足す。

### 3.2 address の配置（MIPS32）
| virt | 物理 | 使い方 |
| --- | --- | --- |
| `0x00000000`〜`0x7fffffff`（kuseg） | TLB | user（page `0x00001000` から） |
| `0x80000000`〜`0x9fffffff`（KSEG0） | 0〜512 MiB、cached | kernel image と direct map。RAM は物理 0〜256 MiB だけ使う（256 MiB 以上は I/O の穴があり、物理 `0x20000000` の alias は KSEG0 に届かない） |
| `0xa0000000`〜`0xbfffffff`（KSEG1） | 0〜512 MiB、uncached | device の窓（GT・PIIX4 の I/O・FPGA・framebuffer） |
| `0xc0000000`〜`0xffffffff`（KSEG2/3） | TLB | kernel の mapped の領域（今回は使わない） |

- `hal_pmem` の phys→virt は物理 < `0x10000000` なら `0x80000000 + P`、device は `0xa0000000 + P`。
- kernel の W^X: KSEG0 は TLB を通らないので **kernel の text の書き込み保護は無い**（ppc32 の BAT と同じ制限、WS264 §2）。
- KSEG2/3 の address の refill（Minor 10）: 今回は kernel の mapped の領域を使わないので、refill は user の root の上の半分（index 512〜1023、常に 0）を見て general の例外へ行き、kernel の fault として panic する。MIPS32 の refill は BadVAddr の bit 31 を見ない（root の上の半分を 0 に保つことで同じ結果になる）。

### 3.3 例外
- EBase（R2 の CP0 15 sel 1）を kernel の `.text` の 4 KiB に揃えた symbol に向け、vector を copy しない（R1・MIPS IV（Qube の RM5230）は EBase が無く `0x80000000` に copy が要る、その時に足す）。**`_start` の最初の命令の列で EBase を書いて BEV=0 にする**（Minor 9。それより前の例外は BEV=1 の `0xbfc00380` の QEMU の小さな bootloader の領域へ飛んで何も出さずに壊れるため、EBase の前には fault しうる命令を置かない）。
- Cause.IV = 0（24Kf に VInt が無い）なので割り込みも +0x180 の general に来る。
- k0・k1 は例外の入口専用。入口: Status.KSU（UM）を見て user からなら percpu の `kernel_sp`（UP なので固定の KSEG0 の変数）へ切り替える。
- trap frame（MIPS32）: 32 個の GPR ＋ hi・lo・EPC・Status・Cause・BadVAddr = 38 word = 152 byte。o32 で C を呼ぶ時は、その下に引数の 16 byte の領域（a0〜a3 の home）を確保する（Minor 10。frame と合わせて 168 byte、8 byte に揃う）。MIPS64 は 38 × 8 = 304 byte（n64 は home の領域が要らない）。
- CP0 の hazard（Minor 10）: `mtc0` の後で結果に依る命令の前に R2 の `ehb` を置く（EntryHi・EntryLo の後の `tlbp`・`tlbwi`・`tlbwr`、`tlbp` の後の Index の読み、Status の後、EBase の後）。`eret` の前の EPC・Status の書き込みは `eret` が hazard を消す。命令の流れを変える CP0 の書き込み（Status.CU1 で FPU を使う直前等）の後は `jr.hb` で戻る。
- 分岐の遅延 slot: Cause.BD が 1 の時の fault は EPC が分岐を指す。user の emulation は作らないので、signal の時の EPC はそのまま。
- system call は `syscall`。HAL の約束（今回は kernel の self-test だけ）: 番号 v0、引数 a0〜a3・t0・t1、戻り値 v0、error の印 a3。EPC は +4 してから戻る。user の ABI は userland の WS で決める。
- 8259 の割り込みの番号（Minor 10）: IP2 で OCW3 の poll（`0x0c` を command port に書き、読んだ値の bit 7 と下 3 bit）で取る。master の 2 番（cascade）なら slave で同じ。GT の IACK（`0xc34`）は使わない。

#### user の Status（M4、権限の昇格を防ぐ）
- user に戻る `eret` の Status は、保存した値をそのまま使わず、毎回次の式で作る:
  `Status = kernel_base | KSU_USER | EXL | IE | (今の Status の IM) | (この task が FPU の owner なら CU1) [| UX（MIPS64）] [| FR（FPU を使える時）]`
  `kernel_base` は §1.6 の確定値（CU0=0・ERL=0・BEV=0）。CU0 は user に決して立てない（user が `mtc0`・`tlbwr` を実行できるため）。
- trap frame の Status は表示と debug の読み出しのためだけに保存する。sigreturn・ptrace（`hal_gpregs.status`）で書かれた値は捨てる（どの bit も受けない。上の式で作るので user が選べる bit は無い）。
- kernel から kernel への例外（入れ子）の戻りは、保存した Status を戻す（kernel が作った値なので消毒は要らない）。ただし IM は今の値に置き換える。
- context switch は Status を保存・復元しない（Linux の r4k の switch が IM・IE を今の値のまま保つのと同じ考え）。割り込みの有効・無効は HAL の irq の save/restore の API が IE だけを扱う。CU1 は lazy FPU の owner の判定で立て下ろしする。

### 3.4 TLB と page table
- page は 4 KiB、EntryHi の VPN2 は 8 KiB の対（偶・奇）。MIPS32 の table は 2 段（10 + 10 + 12）、葉の PTE は 32 bit。MIPS64 は §8。
- **PTE の形（M2）**: `PTE = 物理の page の address（bit 12〜） | 硬い bit（EntryLo の bit 0〜5 を 6 つ左へ: bit 6 G、7 V、8 D、9〜11 C） | software の bit（bit 0〜5）`。
  software の bit: bit 0 PRESENT（map されている）、1 ACCESSED、2 WRITABLE（書いてよい）、3 DIRTY（書かれた）、4 EXEC（exec の意図、記録だけ）、5 予約。
  refill は PTE を 6 bit 右へ shift して（MIPS32 `srl`、MIPS64 `dsrl`）EntryLo にする。物理の page の address が EntryLo の PFN（bit 6〜）の位置に合い、software の bit は shift で落ちて hardware に届かない。
- **V と D の規則**: V = PRESENT ∧ ACCESSED ∧ (prot に READ か EXEC がある)、D = V ∧ WRITABLE ∧ DIRTY。PROT_NONE は PRESENT=1・V=0 で、「無い」（PRESENT=0）と区別できる。
- **accessed の追跡**（Linux の MIPS と同じ考え）: map は ACCESSED=0（V=0）で入れる。最初の読み書きの TLB Invalid（TLBL・TLBS、general の例外）で、PRESENT かつ prot に合えば ACCESSED（書き込みなら WRITABLE を見て DIRTY も）を立て、その対を下の「対の同期」の手順で TLB に書き直して戻る。prot に合わなければ kernel の fault の報告へ。
- **dirty の追跡**: 最初の書き込みの TLB Modified（ExcCode 1）で WRITABLE を見て DIRTY と D を立て、対を書き直す。WRITABLE でなければ書き込みの保護の fault。
- `hal_space_query` は PRESENT・ACCESSED・DIRTY・WRITABLE・prot を software の bit から報告する。`hal_space_clear_flags(ACCESSED)` は ACCESSED と V を、`(DIRTY)` は DIRTY と D を落とし、対の同期を行う。
- **exec の制限（M2）**: 24Kf・MIPS64R2-generic は RI/XI を持たない（Config3.RXI = 0）。読める page は実行もできる（`HAL_SPACE_EXEC` は強制できない、EXEC の bit は記録だけ）。制限として WS の完了の記録にも書く。Config3.RXI = 1 の CPU（後の実機）では PageGrain.XIE・RIE を立て、EntryLo の RI・XI の bit を使う形に後で広げる。
- refill（EBase+0x000、k0・k1 だけを使う、0x80 byte に収める（Minor 10））: BadVAddr → 現在の space の root（UP は固定の KSEG0 の変数）→ `root[va >> 22]` が 0 なら、空の対（EntryLo0/1 = 0、V=0）を入れて戻り、TLB Invalid として general に来させる → そうでなければ偶・奇の 2 つの PTE を shift して EntryLo0/1 に → `ehb` → `tlbwr` → `eret`。refill は TLB に無い時だけ来るので、`tlbwr` で同じ VPN2・ASID が二重になることは無い。
- **対の同期（M1）**: TLB の entry は偶・奇の 2 page を持つので、片方が V=0 のまま入った entry が残りうる（隣の page の refill の時）。そのため PTE を変える全部の操作で、TLB の entry を書き直すか無効にする:
  - 対象: `hal_space_map`・`unmap`・`prot`・`clear_flags`、TLB Invalid・Modified の handler、`hal_space_flush_tlb_range`。
  - 手順（割り込みを止めて）: 今の EntryHi を保存 → EntryHi = VPN2 | その space の ASID → `ehb` → `tlbp` → `ehb` → Index を読む。当たったら、TLB Invalid・Modified の handler と map・prot・clear_flags は table の偶・奇の今の PTE から EntryLo0/1 を作って `tlbwi`。unmap と range の flush は、その entry を「無効の entry」（EntryHi = `0x80000000 + index × 0x2000`（KSEG0、互いに違う VPN）、EntryLo0/1 = 0）で `tlbwi`。→ EntryHi を戻す → `ehb`。
  - space がこの世代の ASID を持たない時は、TLB にその space の当たる entry は無いので何もしない。
  - TLB Invalid・Modified の handler では必ず `tlbp`・`tlbwi` を使い、`tlbwr` を使わない（例外の時点でその VPN2 の entry が TLB にあるので、`tlbwr` は二重の entry を作り、実機では machine check になる）。
- **ASID の世代（M3）**: ASID は 8 bit（EntryHi）。0 は「どの space でもない」に予約し、無効の entry と kernel に使う。
  - 全体で `asid_next`（1〜255）と `asid_generation`（64 bit、1 から）を持つ。space ごとに `(asid, generation)` を持つ。
  - switch の時、space の generation が今の世代と違えば ASID を新しく取る。`asid_next` が上限を超えたら、世代を 1 つ進め、TLB の全 entry を無効の entry で書き（wired は 0）、`asid_next = 1` から配り直す。古い世代の ASID を持つ space は、次に switch した時に新しい ASID を取る。
  - `hal_space_flush_tlb(space)`: その space の generation を 0 にする（ASID を捨てる）。今の space なら、すぐ新しい ASID を取り、EntryHi の ASID を書き替える。TLB に残る古い ASID の entry は、その ASID が次の世代まで再利用されないので当たらない。
  - `hal_space_flush_tlb_range(space, va, len)`: space が今の世代の ASID を持てば、対ごとに「対の同期」の無効化を行う（EntryHi の ASID を一時的にその space の物に替えて `tlbp`）。対の数が TLB の entry 数を超える時は `hal_space_flush_tlb(space)` と同じにする。
  - `hal_space_destroy`: generation を 0 にする（ASID の「解放」は、その ASID を今の世代で二度と配らないことで行う。世代が回る時に TLB の全 flush で消える）。table の page を返す。
- TLB の entry 数は Config1.MMU + 1 から（24Kf 16、MIPS64R2-generic 64）。初期化は全 entry を無効の entry（KSEG0 の互いに違う VPN、V=0、ASID 0）で書いて machine check を避ける。

### 3.5 cache（M7）
- QEMU は cache を模さない（cache 命令は何もしない）が、実機（RG350・CI20・Qube）のために正しく書く。Config1 の IL/IS/IA・DL/DS/DA から line・set・way を求め、way の大きさ = set × line。
- Config.K0 = 3（cacheable noncoherent、write back）を入口で KSEG1 から設定する（入口は 2 = uncached、§1.6）。
- API の写像: D cache の書き出し → `cache Hit_Writeback_Inv_D`（範囲を line ごと）、命令の同期 → R2 の `synci`（`rdhwr SYNCI_Step`）と `sync`・`jr.hb`。DMA の範囲は書き出し・無効化。user の page を実行可能にする時に命令の同期（arm64 の WS044 p009 と同じ規則）。
- **色（aliasing）**: VIPT の cache で way の大きさが 4 KiB を超えると、同じ物理の page が KSEG0 の direct map と user の VA で違う色（VA の bit 12 以上の index の bit）の line に入る。`Hit_*`・`synci` は打った address の色の line しか消さない。
  - 判定: 起動時に D・I の way の大きさを求め、4 KiB を超えれば `cache_alias = true`（色の数 = way / 4 KiB）。24Kf（QEMU）は 1 KiB で aliasing 無し、MIPS64R2-generic（QEMU）は Config1 の上で 8 KiB の 4 way で aliasing あり（QEMU は cache を模さないので表には出ない）。
  - `cache_alias` の時の規則（今回の実装の範囲）: kernel が KSEG0 から user の page に書いた後（copyout・page の 0 埋め・exec の読み込み）と、page を unmap して他へ渡す前に、その物理の page を**全部の色**で書き出し・無効化する（way ごとに、index = 色 × 4 KiB + page の中の offset の `Index_Writeback_Inv_D`）。実行可能にする時は I cache も全部の色で `Index_Invalidate_I`。
  - 制限: 同じ物理の page を違う色の 2 つの user の VA に書き込み可能で map した時（共有の mmap）は一貫しない。色を揃える割り当て（page coloring）は実機の WS（WS273 以降）へ回す。実機での確かめも WS273（RG350 の JZ4770 の cache の形は未確認）。
  - この規則は QEMU では no-op なので、p004 の self-test では呼ばれたことだけを数える。

### 3.6 timer
- CP0 Count（160 MHz、§1.1）と Compare、割り込みは IP7（§1.3）。tick は 1000 Hz（`HAL_TIMER_FREQUENCY`、riscv32 と同じ）、Compare に次の値を書くと IP7 が落ちる。遅れた時は Count から次の境界を求め直す（Compare を過ぎた値に書くと 2^32 cycle 来ない）。
- 64 bit の monotonic の counter は Count の wrap（160 MHz で約 26.8 秒）を tick の度に数えて作る（tick が 26 秒止まらない前提、idle の `wait` でも Compare の割り込みで起きる）。
- idle は `wait`。

### 3.7 atomic
- MIPS32: `ll`/`sc` の 32 bit だけで、64 bit の操作が無い → `HAL_ATOMIC_STYLE_ENTER`（riscv32・ppc32 と同じ）、`hal_atomic_uint_try_acquire` は `ll`/`sc` の swap。
- MIPS64: `lld`/`scd` があるので riscv64 と同じ native の style（§8）。
- `sync` を acquire・release の barrier に使う（R2 の軽い sync の種類は使わない）。

## 4. HAL の API の差分（[proposed/hal-mips.md](proposed/hal-mips.md)）

U1（2026-10-19 14:29 ユーザー、クリック）: 「同じ形で事前承認」（WS263〜266 と同じ形）。適用は p002 の実装で、proposed/hal-mips.md の形のまま。

1. `include/hal/arch.h` に `HAL_ARCH_MIPS32`（`#include <hal/arch/mips32.h>`）と `HAL_ARCH_MIPS64`（`mips64.h`）の節。
2. `include/hal/arch/mips32.h` を新規（riscv32.h と同じ節の並び。spin の hint は `nop`、理由は proposed/hal-mips.md D2）。
3. `include/hal/arch/mips64.h` を新規（同じ並び、64 bit の register、atomic は native）。
4. `hal_get_arch_handoff` の名前付きの値: `boot.command-line`（既存の名前、無い時 NULL）だけ。版 1 の `mips.initrd` は使う側が無いので外した（Minor 4）。initrd を kernel に渡す時に、riscv32 と同じ形（2 つの uint64）・名前（`mips32.initrd`・`mips64.initrd`）で足す案を改めて出す。memory の大きさは汎用の `kern_boot_handoff` の memory map で渡す。
- hal.h・`include/kern/boot.h` は変えない。

## 5. HAL の外の変更（共通）

- `include/uapi/stat.h`: o32 は 64 bit の member を 8 byte に揃える（*）ので riscv32・ppc32 の枝の条件に `KERN_USER_ABI_MIPS32` を足す（riscv32・ppc32 と同じ 92 byte、design-review-1 で確認）。n64 は LP64 の既存の枝（`KERN_USER_ABI_MIPS64` を riscv64 と同じ側に）。userland が無いので ABI は未使用。
- `include/kern/elf.h` に `EM_MIPS`（8）、`src/kern/elf.c` に `HAL_ARCH_MIPS32`（ELF32、little endian）・`HAL_ARCH_MIPS64`（ELF64）の枝。
- kernel の platform の binding（Minor 3）: riscv32・ppc32・m68k と同じく `src/kern/platform/hal-console.c` を使う（platform/riscv32/vmunix.mk 52 行と同じ形）。版 1 の `riscv-virt.c` は riscv64 の FDT 用なので使わない。PCI の bus の driver は今回は作らない（VGA は HAL が直に扱う）。
- `Makefile`・`tools/menuconfig.py` に `mips32-malta`（arch `mips32`、board `malta`、build `build/mips32-malta`）・`mips64-malta`。`platform/mips32-malta/`・`platform/mips64-malta/`。試験の config は `plan/ws271/tests/config-mips32-malta.mk`（p002 で作る。形は plan/tools/ports/config-riscv32-virt.mk と同じ、driver は全部 n）。`NO_DESKTOP_ARCHITECTURES` に mips32・mips64。

## 6. 試験（共通）

- `plan/tools/boot-test.sh` に `BOOT_MODE=mips32-malta`・`mips64-malta`（**Q1 が変える**、案は [proposed/boot-test-mips.md](proposed/boot-test-mips.md)。usage の comment、既定の operand と interval の case、qemu と required の case、disk を作らない mode の列、起動の行の 5 か所）:
  `qemu-system-mipsel -M malta -m ${BOOT_MEMORY:-256M} -vga std -display none -no-reboot -serial unix:…,server,nowait -qmp unix:…,server,nowait -kernel "$image" [-append "$BOOT_APPEND"]`。
  64 bit は `qemu-system-mips64el -M malta -cpu MIPS64R2-generic`。既存の mode の振る舞いは不変、判定は QMP の screendump の PNG。
- 判定（T1）: 画面に `MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK`、`MIPS32 CPU 1 ONLINE`、`VFS initialization failed`（64 bit は `MIPS64`）。`PAGING PASS` は §6.1 の self-test が全部通った時だけ出す。
- 解析は gdbstub（`-S -gdb tcp::<port>`、host の `gdb-multiarch`（host に有り、`set architecture mips:isa32r2` を受けると 2026-10-19 に確かめた））。

### 6.1 self-test の中身と PASS の条件（M9）
前例の ppc32/space.c 149 行は何も確かめずに `PAGING PASS` を出していた。MIPS は次を全部確かめ、全部通った時だけ `PAGING PASS` を、どれかが落ちたら `PAGING FAIL <番号>` を画面に出して止まる（判定の文字列が出ないので T1 で FAIL になる）。kernel mode から kuseg の address を使う（user の process が無くても TLB を通る）。refill・TLB Invalid・TLB Modified・fault の数は HAL の内部の counter で数える。

| # | 確かめる事 | 手順と PASS の条件 |
| --- | --- | --- |
| 1 | map の後の refill | space A に page P（偶）を map、P に書いて読む。値が一致し、refill の数が 1 増える |
| 2 | 片側だけ V=0 の対（M1） | P+4 KiB（奇）は map しないまま P を読んで対を TLB に入れる → 奇を map → 奇に書いて読む。値が一致し、TLB Invalid が無限に続かない（その間の TLB Invalid の数が 2 以下） |
| 3 | accessed（M2） | map 直後の query で ACCESSED=0、読んだ後 1。clear_flags(ACCESSED) の後 0 になり、再び読むと 1 |
| 4 | TLB Mod による dirty | 読んだだけの page の query で DIRTY=0、書いた後 1、TLB Modified の数が 1 増える。WRITABLE でない page への書き込みは保護の fault（試験の fault の hook で受けて数え、その命令を飛ばす） |
| 5 | PROT_NONE と unmap | prot を NONE にした page の読みは保護の fault、unmap の後の読みは「無い」の fault（2 つが区別される） |
| 6 | 2 つの space の ASID | space A・B の同じ VA に違う物理の page を map し、A → B → A と switch しながら読む。各 space の値が出る |
| 7 | 世代が回る（M3） | 試験だけ ASID の上限を 4 に下げる（HAL の内部の変数）。5 つの space を順に 2 周 switch して読む。世代が 1 以上進み、全部の値が正しい。終わったら上限を戻す |
| 8 | flush と destroy（M3） | 今でない space B を `hal_space_flush_tlb` → B に switch して読む（新しい ASID、値は正しい）。range の flush の後の読みで refill が起こる。destroy した space の ASID が同じ世代で他の space に配られない（ASID の値を比べる） |
| 9 | FPU の lazy（M5） | 2 つの kernel の task が違う値を `$f0`〜`$f31` と FCSR に入れて交互に switch し、各 task で値が戻る。片方の FCSR に Cause と Enable の同じ bit を立てた値を消毒の経路で書いて戻しても FPE が起こらない（FPU を使えない CPU では `FPU NONE` と出して飛ばす） |
| 10 | user の Status（M4） | user に戻る Status を作る関数に CU0・KSU=kernel・ERL を立てた frame を渡し、結果が CU0=0・KSU=user・EXL=1・ERL=0 になる（関数の単体の確かめ、eret はしない） |

TIMER TICK は Count/Compare で 10 tick を数えた時に出す（既存の port と同じ）。self-test の code は p004 で書き、試験だけの経路（ASID の上限の変更、fault の hook）は `HAL_MIPS_SELFTEST` の時だけ build する。

## 7. WS271（mipsel）に固有

### 7.1 link と入口
- `0x80100000`（KSEG0）に link した ELF をそのまま `-kernel` に（flat image は要らない）。物理 `0x00100000`〜。物理 `0x2000` の PROM の表は入口で HAL の bss に写してから使う。
- 起動の段（Minor 9 で順を変えた）: (1) `_start`（KSEG0）: **最初に EBase を自分の vector に書いて `ehb` の後 BEV=0**、Status を確定（§1.6）、Config.K0 を 3 に（KSEG1 から）、bss を消す、stack。(2) `mips_cmain`: a0〜a3 を受け、argv・環境を写し、initrd の前置きを解く（§1.6）。(3) `bsp-malta` が PCI の VGA を設定し console を出す。(4) cache の形と色（§3.5）、FPU の有無（§2.3）、TLB の初期化、self-test（§6.1）、timer、割り込み（8259 は全部 mask、timer だけ）。(5) kernel へ。
- memory は `memsize`（無ければ a3）の大きさで物理 0 から。kernel の image、物理 0〜1 MiB（ENVP の `0x2000` を含む）、initrd の範囲を除く。

### 7.2 制限
- UP。外部割り込み（8259）の source の登録は作る（PIIX4 の IDE・USB は driver が無いので使わない）。reset は FPGA の SOFTRES（`0x1f000500` に `0x42`、malta.c 500〜502 行で確認）、poweroff は止まるだけ。kernel の W^X 無し。user の exec の強制無し（§3.4）。FPU の Unimplemented Operation は SIGFPE（§2.3）。cache の色の共有の mmap の制限（§3.5）。

## 8. WS272（mips64el）の差分

| 項目 | MIPS32（WS271） | MIPS64（WS272） |
| --- | --- | --- |
| QEMU | `qemu-system-mipsel -M malta`（24Kf） | `qemu-system-mips64el -M malta -cpu MIPS64R2-generic`（U4。MIPS64R2、FPU、TLB 64 entry、SEGBITS 42・PABITS 36、CCRes 2 → Count 160 MHz、cache 256 set × 32 byte × 4 way（way 8 KiB、§3.5 の色あり）。cpu-defs.c.inc 672〜697 行で確認、ws272-p001。既定の 20Kc は R1 で EBase・`ext` が無いので使わない） |
| ABI | o32、`-mabi=32` | n64、`-mabi=64`、`-march=mips64r2`、`-msym32`（受けた時だけ、§2.2） |
| linker | `elf32ltsmip` | `elf64ltsmip` |
| link | `0x80100000` | `0xffffffff80100000`（CKSEG0）。QEMU の `cpu_mips_kseg0_to_phys` は 64 bit でも同じ物理 `0x00100000` に置く（*） |
| 受け渡し | a0〜a3 は 32 bit | 同じ値を 64 bit に符号拡張（bootloader の `bl_gen_load_ulong` が ISA_MIPS3 で `dli`、bootloader.c で確認。`ENVP_VADDR` 自体が符号拡張の形かは *）。表の pointer は 32 bit の KSEG0 のままなので、表の entry は 32 bit で読み符号拡張する |
| Status | — | UX=1（user の 64 bit の address）、KX=1、SX=0、FR=1。entry では KX=0（§1.6 と同じ reset の値）なので XKPHYS に触れる前に KX を立てる |
| 物理の範囲（M8） | 物理 0〜`min(RAM, 256 MiB)` | **正の物理の範囲を 1 つに決める**: 物理 0〜`min(RAM, 256 MiB)`（low の alias）と、RAM が 256 MiB を超える時は物理 `0x80000000 + 256 MiB`〜`0x80000000 + RAM`。物理 `0x80000000`〜`+256 MiB`（low の alias と同じ RAM）と `0x20000000` の alias は memory map に入れない。kernel の image（物理 `0x00100000`〜）と ENVP（`0x2000`）はどちらも low の alias の側の物理なので、その範囲で予約すれば足りる |
| direct map | KSEG0（256 MiB まで） | XKPHYS（`0x9800000000000000 + P` cached、`0x9000000000000000 + P` uncached）で上の範囲の全部。kernel の image の virt（CKSEG0）→ 物理は `virt − 0xffffffff80000000`（low の alias の側）、direct map の virt → 物理は `virt − 0x9800000000000000`。どちらの物理も正の範囲に入る。Config3.LPA=1 だが RAM は 32 bit の物理の中なので PageGrain.ELPA は立てない |
| user | kuseg 2 GiB | xuseg（SEGBITS 42 = 4 TiB。user は下の 512 GiB だけ使い、3 段の table（9 + 9 + 9 + 12）、PTE 64 bit、§3.4 と同じ形を `dsrl 6`） |
| refill | EBase+0x000、`tlb32.S` | EBase+0x080 の XTLB refill、`tlb64.S`（XContext の BadVPN2 は使わず BadVAddr から歩く）。BadVAddr が 512 GiB 以上なら空の対を入れず general の例外へ送り、fault にする（Minor 10） |
| atomic | enter の style | native（`lld`/`scd`） |
| trap frame | 168 byte（home の 16 byte を含む） | 304 byte |
| arch の header | `mips32.h` | `mips64.h` |
| ELF | ELF32 | ELF64、`EM_MIPS` |
| int64.c | 要る | 要らない |

WS272 の p001 は、この表と §1〜§6 を読んで自分の CPU の型の事実を source で確かめ、残りを gdb で確かめる計画を phase.md に書いた（[ws272-p001](../ws272/phase001/phase.md)）。M8 の物理の範囲は WS272 の p002 の前に確定させる条件（design-review-1）で、この版で確定した。

## 9. Phase と LW（8 LW ≈ 2〜3 時間）

Minor 13 を採り、版 1 の p003（20 LW）を 2 つに分けた。

### WS271
| Phase | 目的 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 | 4 | — |
| p002 | 入口・console の画面: `src/hal/mips/` の locore・asm.h・cmain、bsp-malta の pci・framebuffer・cons、build の配線（Makefile・menuconfig・platform・config）。到達点は画面の `MIPS32 HAL` の行（T1） | 12 | LLVM の Mips（Q1）、boot-test の mode（Q1）。HAL の API は U1 で承認済み |
| p003 | 例外・timer・8259・task: trap の入口と出口、user の Status（§3.3）、Count/Compare、8259 の poll、context switch。到達点は画面の `TIMER TICK`（T1） | 10 | p002 |
| p004 | TLB・space・FPU・atomic と self-test（§3.4・§3.5・§2.3・§6.1）、VFS の段まで。到達点は `PAGING PASS` と `VFS initialization failed`（T1） | 12 | p003 |
| p005 | 規約の全文の見直し | 4 | p004 |

### WS272
| Phase | 目的 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | §8 の確認の計画 | 2 | WS271 p001 |
| p002 | 64 bit の入口・console（共通の層を `HAL_ARCH_MIPS64` で build、XKPHYS、CKSEG0、§8 の物理の範囲） | 8 | WS271 p002、LLVM の Mips。HAL の API（D1・D3）は U1 で承認済み |
| p003 | XTLB refill・3 段の table・native atomic、self-test の 64 bit の版、VFS の段まで | 10 | WS271 p004、WS272 p002 |
| p004 | 規約の全文の見直し | 3 | p003 |

## 10. ユーザーの判断（2026-10-19 14:29 ユーザー、クリック、決定済み）

| # | 問い | 決定 |
| --- | --- | --- |
| U1 | HAL の API の差分（§4）を 2026-10-18 の新しい port の事前の承認と同じ扱いにするか | 「同じ形で事前承認」 |
| U2 | toolchain を LLVM の Mips で待つか GCC で始めるか | 「LLVM の Mips を待つ」 |
| U3 | user の hardfloat の ABI | 「FPXX（FR=1）」。前提の FCR0.F64 を WS273 で確かめる（§2.3、M6） |
| U4 | MIPS64 の QEMU の CPU | 「MIPS64R2-generic」 |
| U5 | 画面の大きさ | Q1 が既定の 640x480 |

## 11. Q1 への依頼

1. toolchain: [proposed/llvm-mips.md](proposed/llvm-mips.md) の案（Q1 が作り直し中）。
2. tools: boot-test.sh の `mips32-malta`・`mips64-malta`（[proposed/boot-test-mips.md](proposed/boot-test-mips.md)）。host の `gdb-multiarch` は有る（確認済み）。
3. Guardrail・Master の Tools の登録（mode の追加の後）。
4. WS271 の Phase の表の分割（p003〜p005）に合わせた Queue と Master の写し。

## 12. レビュー 1 への対応

[design-review-1.md](design-review-1.md) の全項目。

| 項目 | 対応 | 所 |
| --- | --- | --- |
| M1 片側 V=0 の対が古いまま | 直した: PTE を変える全部の操作と Invalid・Mod の handler で `tlbp`→`tlbwi`（書き直しか無効の entry）。handler で `tlbwr` を使わない | §3.4「対の同期」 |
| M2 accessed・PROT_NONE・exec | 直した: software の PRESENT・ACCESSED・WRITABLE・DIRTY・EXEC、V=0 で入れて最初の Invalid で accessed。exec は強制できない制限を明記 | §3.4、§7.2 |
| M3 今でない space の flush・ASID の再利用 | 直した: space ごとの (ASID, 世代)、flush は ASID を捨てる、range は一時の EntryHi で `tlbp`、destroy は世代で解放 | §3.4「ASID の世代」 |
| M4 user の Status | 直した: 毎回 mask の式で作る、CU0 は立てない、sigreturn・ptrace の値は捨てる、switch で Status を保存しない | §3.3「user の Status」、§1.6 |
| M5 FCSR の Cause | 直した: `ctc1` の全部の場所で Cause を消す。Unimplemented は SIGFPE の制限、FS は 0 のまま | §2.3 |
| M6 FCR0.F64 | 直した（制限と引き継ぎ）: Config1.FP と F64 の両方で FPU を使う。無ければ FPU 無しの扱い。JZ4770 は WS273 で確かめる | §2.3、§10 U3 |
| M7 cache の aliasing | 直した（制限と引き継ぎ）: 色の判定、全部の色の Index の書き出し、共有の mmap の制限、page coloring と実機の確かめは WS273 へ | §3.5 |
| M8 MIPS64 の 2 つの物理 | 直した: 正の物理の範囲を low の alias ＋ `0x80000000+256MiB` 以上に決め、image・ENVP を low 側で予約 | §8、ws272/ws.md、ws272-p001 |
| M9 試験で M1〜M5 を見つけられない | 直した: 10 項目の self-test と PASS・FAIL の条件 | §6.1 |
| Minor 1 source で分かる事実 | 直した: 入口の Status・K0・EBase・IP7・SOFTRES・PCI の窓・VGA の slot を source の行で書き、未確認の印を外した（P1 が写しの行を読み直した） | §1.2、§1.3、§1.6、§7.2 |
| Minor 2 sun4u の前例 | 直した: 手本を sun4u の framebuffer.c に | §1.5 |
| Minor 3 platform の file 名 | 直した: `hal-console.c` | §5 |
| Minor 4 handoff の契約 | 直した: NULL を返す形、`mips.initrd` を外した | §4、proposed/hal-mips.md D4 |
| Minor 5 cmdline・initrd | 直した: 前置きを取り除き、initrd の範囲を memory map から除く | §1.6、§7.1 |
| Minor 6 toolchain の未検証 | 直した: `.set fp=64`・`oddspreg`、LTO と `.sdata` の確かめ、`-msym32` は受けた時だけ | §2.2 |
| Minor 7 softfloat.mk | 直した: 既存の仕組みと 64 bit の除算の helper を書いた | §2.3 |
| Minor 8 `pause` の理由 | 直した: R2 の基本の命令（`sll $0,$0,5`）、LLbit を待つ意味なので `nop` を選ぶ | proposed/hal-mips.md D2 |
| Minor 9 起動の順 | 直した: `_start` の最初に EBase と BEV=0 | §3.3、§7.1 |
| Minor 10 細部 | 直した: refill の 0x80 byte、KSEG2/3 の扱い、hazard、o32 の home の 16 byte、8259 の poll、xuseg の 512 GiB の拒否 | §3.2〜§3.4、§8 |
| Minor 11 文書の不整合 | 直した: 「§7.5」を外した、版の表記を v10.0.11 に揃えた、5 つの SHA-256 を記録 | §0、§1.1 |
| Minor 12 boot-test の案 | 直した: 2 番目の case、interval、usage の comment、disk の列、起動の行の全部を案に。console の順は未確認のまま（§1.5） | proposed/boot-test-mips.md、§6 |
| Minor 13 Phase | 直した: WS272 p002 の依存に承認済みを書いた。WS271 p003 を 2 つに分け p005 を規約に | §9、ws271/ws.md、ws272/ws.md |
| Minor 14 toolchain の現状 | 直した: 今の状態を書いた | §2.1 |
