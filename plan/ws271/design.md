<!-- awesome-plan project=zedbsd record=ws271-p001-design -->

# WS271・WS272 の設計: MIPS32（mipsel）・MIPS64（mips64el）の QEMU malta（framebuffer の console・VFS の段まで）

2026-10-19 P1（q1273、[ws271-p001](phase001/phase.md)）。親: [WS271](ws.md)（mipsel）・[WS272](../ws272/ws.md)（mips64el）。
手本は [WS263](../ws263/design.md)（riscv32）・[WS264](../ws264/design.md)（ppc32）・[WS265](../ws265/design.md)・[WS266](../ws266/design.md)。
§1〜§6 は MIPS32・MIPS64 の共通、§7 が WS271 に固有、§8 が WS272 の差分。WS272 の p001 は §8 の確認だけで済む形にした。

## 0. 出典と確認の水準

- QEMU の source（host の QEMU は Debian の 10.0.11、取得は upstream の tag `v10.0.11`。`hw/mips/malta.c` は v10.0.3 と同一）。
  取得先 `https://gitlab.com/qemu-project/qemu/-/raw/v10.0.11/<path>`、置き場 `build/tmp/ws271-docs/`（worktree p1、git に入れない）:

  | path | SHA-256 |
  | --- | --- |
  | hw/mips/malta.c | b5066d887fa1218c9deb140c6ac71d79b6c957915f9829646f67c7c35b1d4830 |
  | hw/mips/bootloader.c（v10.0.3） | d14c0f39609a9e4b55172344b04402066576a9adda6f7c00d2cd47108c5a791b |
  | hw/pci-host/gt64120.c（v10.0.3） | d01b13236f0fc28b34115c68d59f8777ee7bed130780387c380f72f239800994 |
  | hw/isa/piix.c（v10.0.3） | 20f9adccce3f270695b4315444cd2a80f60704224becb933acf6fbfe975d67d9 |
  | hw/char/serial.c（v10.0.3） | 46548454bc48e12b430795fc69cb19f0349bbef3a63ee37c23aa365713978b91 |
  | target/mips/cpu-defs.c.inc（v10.0.3） | a29998857ec9c02aa11c7ab3d305510f49fb8e12d9051ab25a8a08fb31b5afcd |
  | target/mips/cpu.c・target/mips/system/cp0_timer.c・hw/display/vga-pci.c・system/vl.c・hw/intc/i8259.c | v10.0.11、SHA-256 は p002 の着手で `sha256sum` を phase.md に写す（未記録） |

  QEMU は GPL-2.0。値（address・register の offset・bit）は事実として使い、code・名前・構造は写さない（[Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」、comment は block ごとの `Value(s) referenced:`）。
- MIPS の公開の資料: MIPS32/MIPS64 Architecture for Programmers Vol. III（Privileged Resource Architecture、PRA）、MIPS32 24K の Software User's Manual、Malta の User's Manual、GT-64120 の datasheet。**この Phase では文書の取得をしていない**（network の取得は QEMU の source だけ）。PRA の事実（KSEG の配置、vector の offset、EntryLo の形）は広く知られた architecture の定義として書き、p002 の着手で PRA の版を phase.md に記録する（未確認の印 *）。
- 「未確認」は source でも資料でも確かめていない事。p002 で gdbstub（`-S -gdb`）と monitor（`info registers`・`info mtree`・`info pci`・`xp`）で確かめる（T1 か、gdb は実装の担当が T1 に頼む）。

## 1. QEMU malta の事実

### 1.1 machine と CPU
- `qemu-system-mipsel -M malta`（malta が既定の machine）。CPU の既定は 32 bit の build で `24Kf`、64 bit の build で `20Kc`（malta.c `mips_malta_machine_init`）。
- `24Kf`（cpu-defs.c.inc）: `CPU_MIPS32R2 | ASE_MIPS16`、FPU あり（Config1.FP、FCR0 に F64・L・W、つまり FR=1 が使える）、TLB 16 entry（Config1.MMU = 15）、`CCRes = 2`、SEGBITS 32・PABITS 32、Config3.VInt = 0（vectored interrupt 無し）。
- CPU の clock は 320 MHz（`mips_malta_instance_init` の `cpu-refclk`）。CP0 Count は clock を CCRes で割った速さ（cpu.c `mips_cp0_period_set`）→ **Count は 160 MHz**。HAL は board の値として 160 MHz を持ち、実機では別の方法で測る（§7.5）。
- SMP: `-smp` > 1 は CPS を持つ CPU（I6400・P5600 等）でだけ CPS で作られ、24Kf では CPU が複数作られても割り込みは最初の CPU だけ（`create_cpu_without_cps`）。**UP で作る**。
- memory は最大 2 GiB。RAM の全体を物理 `0x80000000` に置き、先頭の `min(RAM, 256 MiB)` を物理 0 に alias、512 MiB を超える分を物理 `0x20000000` に alias（malta.c `mips_malta_init`）。既定の RAM の大きさは machine に指定が無く QEMU の既定（128 MiB、未確認*）。試験は `-m 256M`。

### 1.2 物理の memory map（32 bit の物理）
| 物理 | 中身 | 出典 |
| --- | --- | --- |
| `0x00000000`〜`0x0fffffff` | RAM の先頭 256 MiB の alias | malta.c |
| `0x00002000` | `-kernel` の時の PROM の引数と環境の表（16 entry × (4 byte の pointer ＋ 256 byte)） | malta.c `ENVP_PADDR` |
| `0x10000000`〜 | PCI0 の memory 窓 0（bootloader が設定、終わりは下の注） | malta.c `bl_setup_gt64120_jump_kernel` |
| `0x18000000`〜`0x181fffff` | PCI0 の I/O 窓（ISA の port n は物理 `0x18000000 + n`） | 同上の comment |
| `0x18200000`〜 | PCI0 の memory 窓 1 | 同上 |
| `0x1be00000` | GT-64120 の register（reset の `0x14000000` から bootloader が移す） | 同上 |
| `0x1e000000` | flash（4 MiB） | malta.c `FLASH_ADDRESS` |
| `0x1f000000` | FPGA（LED・ASCII の表示・reset の register 等）、`+0x900` に CBUS UART（16550、reg shift 3、`serial_hd(2)`、CPU の IP4） | malta.c `malta_fpga_init` |
| `0x1fc00000` | reset の vector（flash の写し、read-only。`-kernel` の時は QEMU が書いた小さな bootloader） | malta.c `RESET_ADDRESS` |
| `0x80000000`〜 | RAM の全体 | malta.c |

注: 窓の終わりの値は bootloader の comment（「io window to 0x18000000-0x181fffff」）による。memory 窓 0・1 の長さは GT の register の値の解釈（`(HD + 1 - (LD & 0x7f)) << 21`、gt64120.c `gt64120_pci_mapping`）で決まり、**数値は未確認**（p002 で `info mtree` で確かめる）。PCI の bus address と CPU の物理 address は同じ（alias の offset が CPU の address、gt64120.c）。

### 1.3 PCI（GT-64120）と PIIX4
- config 空間は GT の `0x1be00000 + 0xcf8`（address）・`+ 0xcfc`（data）、PC と同じ形の type 1 の address（bit 31 enable、bus・device・function・register）。bootloader は CPU の endian のまま（`tswap32`）書くので、mipsel では普通の 32 bit の store でよい（gt64120.c は data に `pci_host_data_le_ops`）。
- GT の他の register は「bus の endian は常に逆」（bootloader の comment、gt64120.c `gt64120_writel` は GT_CPU の bit 12 が 0 なら byte を入れ替える）。HAL は GT の他の register に触れない。
- device: PIIX4 は bus 0 の device 10（function 0 ISA・1 IDE・2 USB・3 PM）、AMD 79C973（pcnet）は slot 11、VGA は `pci_vga_init` が空いた slot に置く（**slot 番号は未確認、HAL は bus 0 を走査する**）。
- PCI の割り込み: PIIX4 の PIRQ A〜D を ISA IRQ 10・10・11・11 に向ける（bootloader が PIIX の PIRQC に書く）。slot ごとの pin の写像は malta.c `malta_pci_slot_get_pirq`（10→3、11→1、12→2、18〜21 は回転）。
- 割り込み controller: PIIX4 の中の 8259 の対（ISA port `0x20`・`0xa0`、物理 `0x18000020`・`0x180000a0`）が CPU の IP2（Cause の bit 10、hw 0）に入る（malta.c `*i8259_irq = env->irq[2]`）。CBUS UART は IP4、CP0 の timer は IP7（IntCtl.IPTI、*）。
- **firmware（YAMON）は `-kernel` の時は走らない**。PCI の BAR の割り当て・command register の enable は誰もしない → HAL が自分で割り当てる（§1.5）。

### 1.4 UART
- CBUS UART（`0x1f000900`、16550、register の間隔 8 byte）と、Super I/O（SMS FDC37M817、ISA の上、COM1 = port `0x3f8` = 物理 `0x180003f8`、`serial_hd(0)`）。
- serial は判定に使わない（AGENTS.md）。boot-test の対話の serial は `-serial` の 1 番目 = Super I/O の COM1（未確認*: `serial_hd(0)` が COM1 に繋がる事は malta.c に Super I/O を `isa_create_simple` で作るとしか無い）。

### 1.5 画面（PCI の std VGA、Bochs の linear framebuffer）
- 既定の VGA は machine に `default_display` が無いので cirrus（system/vl.c `get_default_vga_model`）。**boot-test は `-vga std` を付ける**（device `VGA`、1234:1111）。
- std VGA（vga-pci.c）: BAR0 = VRAM（prefetchable memory、既定 16 MiB）、BAR2 = MMIO 4 KiB（`+0x400` VGA の I/O port の写し（port − 0x3c0）、`+0x500` Bochs の DISPI register（index i を `+0x500 + 2i` の 16 bit、little endian）、`+0x600` QEMU の拡張（byte order））。offset の値は vga-pci.c の `PCI_VGA_*_OFFSET`（header 側の定義、数値は*）。
- HAL の手順（bsp-malta の `framebuffer.c`）:
  1. bus 0 の device 0〜31 を走査して 1234:1111 を見つける。
  2. BAR0 に `0x10000000`、BAR2 に `0x11000000` を書き（大きさは all-ones の probe で確かめる）、command に Memory Space を立てる。
  3. DISPI: ENABLE=0、XRES=640、YRES=480、BPP=32、ENABLE = ENABLED | LFB_ENABLED（0x41）。
  4. 属性 controller の index port（`0x3c0`、BAR2 `+0x400`）に `0x20`（palette の access を終えて画面を出す。要否は未確認、p002 の画面で確かめる）。
  5. framebuffer は物理 `0x10000000` を **KSEG1（uncached、`0xb0000000`）** から描く（実機で cache の書き出しが要らない。QEMU では速さの差は小さい）。
  - console は 640x480 XRGB8888、8x16 の font で 80x30。font・cons は既存の bsp の物（riscv32 の bsp-virt の cons/font）を写す。

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
- 入口の CP0 の状態（Status の ERL・BEV・KSU、Config.K0）は **未確認**（reset の直後の bootloader は Status を書かないと読める。p002 で gdb で entry の `info registers` を見る）。HAL は入口で Status を確定した値に書く（ERL=0、EXL=0、BEV=0（EBase を設定した後）、KSU=kernel、IE=0、CU0=1、CU1=0、FR=1）。
- command line は argv[1] から取り、`hal_get_arch_handoff("boot.command-line")`（ppc32 と同じ名前）で kernel に渡す。initrd は `rd_start`・`rd_size` を HAL が解いて `mips.initrd`（物理の範囲）に（今回の到達点では未使用）。

## 2. toolchain（共通）

### 2.1 LLVM（Q1 が適用、ユーザーの許可 2026-10-19）
- 共有の LLVM は今 `LLVM_TARGETS_TO_BUILD='PowerPC;AArch64;RISCV;X86'`（toolchain/llvm/llvm.mk 361 行）で、Mips の target が無い（`clang --print-targets` で確かめた）。
- 案（[proposed/llvm-mips.md](proposed/llvm-mips.md)、Q1 が適用、patch level を `zedbsd9` → `zedbsd10`）:
  1. llvm.mk の targets に `Mips` を足す。
  2. `0001-add-zedbsd-x86-target.patch` と同じ形で、`clang/lib/Basic/Targets.cpp` の mips・mipsel・mips64・mips64el の switch に `case llvm::Triple::ZedBSD: ZedBSDTargetInfo<MipsTargetInfo>`（`__ZEDBSD__` を定義）、`ToolChains/ZedBSD.cpp` の linker の emulation: mipsel o32 → `elf32ltsmip`、mips64el n64 → `elf64ltsmip`（big endian は `elf32btsmip`・`elf64btsmip`、今回は作らない）。
  3. triple: `mipsel-unknown-zedbsd`（o32）・`mips64el-unknown-zedbsd`（n64、`-mabi=64` を既定に）。
- **LLVM の適用までの kernel**: riscv32・ppc32 と同じく `--target=mipsel-unknown-elf -D__ZEDBSD__=1` で build できる（zedbsd の triple は userland が要る時まで要らない）。kernel の build に要るのは Mips の target だけ。
- 代わりの道（Q1 の判断）: Debian の `gcc-mipsel-linux-gnu`（m68k の WS266 と同じ形、host に未導入）。LLVM の再 build を待たずに p002 を始められる。既定は LLVM（ユーザーの許可どおり）。

### 2.2 kernel の flags（mipsel）
`--target=mipsel-unknown-elf -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls -fno-pic -G 0 -mno-gpopt -ffreestanding -D__ZEDBSD__=1`、
`ld.lld -m elf32ltsmip`、link は KSEG0 の `0x80100000`（§7.1）。kernel は浮動小数点を使わないので常に `-msoft-float`（FPU の保存・復元の asm だけ `.set hardfloat`）。
64 bit の除算・shift の helper は riscv32 と同じく `src/libc/int64.c` を LTO 無しで入れる。

### 2.3 浮動小数点の ABI（2026-10-19 ユーザー「まずはhardfloatでOKです。」「softfloatをオプションで選べるようにしたいですね。」）
- 既定 hardfloat: user は o32 の `-mhard-float -mfpxx`（FR=0・FR=1 のどちらでも動く code）。HAL は常に Status.FR=1 で 32 個の 64 bit の FPR と FCSR を保存する（FP32 の code（FR=0 専用）は扱わない）。FPU は lazy: CU1 を 0 にしておき、Coprocessor Unusable（CE=1）で有効にして復元する。
- softfloat の option の置き場所（今回は作らない、決めておくだけ）:
  - config の変数 `ZEDBSD_FLOAT_ABI := hard | soft`（`tools/menuconfig.py` の mips の platform の時だけ出す、既定 hard）。Makefile は userland と libc の CFLAGS に `-mhard-float -mfpxx` か `-msoft-float` を足す。kernel は不変（常に soft）。
  - sysroot は ABI ごとに別の directory（`build/<platform>/sysroot` の名前に `-softfloat` を足す。toolchain の `sysroot.mk` の変更は Q1）。
  - HAL: soft の build では CU1 を立てない。user が FPU の命令を使えば Coprocessor Unusable → SIGILL（hard と同じ handler で、`HAL_MIPS_FPU` が 0 の時）。FPU の無い CPU（Octeon の一部、未確認）は Config1.FP = 0 を見て同じ扱い。
  - ELF の ABI の flag（`EF_MIPS_*`、`.MIPS.abiflags` の fp_abi）で kernel の exec が hard/soft の不一致を拒む（userland の WS で）。

## 3. HAL の構成（共通の層 `src/hal/mips/`）

### 3.1 directory
```
src/hal/mips/            MIPS32・MIPS64 の共通（register 幅は asm.h の macro で切り替え）
  asm.h                  REG_S/REG_L/PTR_ADDU/LONGSIZE、CP0 の register の番号と bit（独自の名前）
  locore.S               _start、Status の確定、bss、stack、EBase の設定、C へ
  vectors.S              EBase+0x000 TLB refill、+0x080 XTLB refill（64）、+0x100 cache error、+0x180 general
  trap.S                 general の入口・出口（frame の保存・復元、eret）
  tlb32.S / tlb64.S      refill の handler（§3.4）、tlb の index・probe・write の小さな関数
  int.c                  Cause.ExcCode ごとの dispatch（Int・Mod・TLBL・TLBS・AdEL・AdES・Sys・Bp・RI・CpU・Ov・Tr・FPE）
  tlb.c                  ASID の割り当て、flush、wired entry
  space.c / page.c       page table（§3.4）、hal_space_*
  cache.c                Config1 から line の大きさ・set・way、§3.5
  task.c / task.S        context switch（s0〜s7・fp・sp・ra・Status）、新しい task の入口
  fpu.c / fpu.S          lazy FPU（§2.3）
  clock.c                CP0 Count/Compare の tick（§3.6）
  atomic.c / spin.h      ll/sc
  percpu.c / lib.c / debug.c / cmain.c
src/hal/mips/bsp-malta/  board.c（handoff・環境の表・reset/poweroff）、pci.c（GT の config）、framebuffer.c、cons.c、font.c、
                         irq.c（PIIX4 の 8259）、uart.c（debug だけ）
```
共通にする理由: WS272 は同じ file を `HAL_ARCH_MIPS64` で build し、差分は refill（`tlb64.S`）・address の配置（§8）・`asm.h` の幅だけ。RG350（WS273）は `bsp-jz4770/` を足す。
既存の riscv32 は riscv64 の写しにした（WS263 §3）が、MIPS は最初から共通の層にする（後で写しを統合する手間を避ける）。

### 3.2 address の配置（MIPS32）
| virt | 物理 | 使い方 |
| --- | --- | --- |
| `0x00000000`〜`0x7fffffff`（kuseg） | TLB | user（page `0x00001000` から） |
| `0x80000000`〜`0x9fffffff`（KSEG0） | 0〜512 MiB、cached | kernel image と direct map。RAM は物理 0〜256 MiB だけ使う（256 MiB 以上は I/O の穴があり、物理 `0x20000000` の alias は KSEG0 に届かない） |
| `0xa0000000`〜`0xbfffffff`（KSEG1） | 0〜512 MiB、uncached | device の窓（GT・PIIX4 の I/O・FPGA・framebuffer） |
| `0xc0000000`〜`0xffffffff`（KSEG2/3） | TLB | kernel の mapped の領域（今回は使わない。512 MiB を超える物理の device や high memory の時に使う、kernel の table は `G` bit） |

- `hal_pmem` の phys→virt は物理 < `0x10000000` なら `0x80000000 + P`、device は `0xa0000000 + P`。
- kernel の W^X: KSEG0 は TLB を通らないので **kernel の text の書き込み保護は無い**（ppc32 の BAT と同じ制限、WS264 §2）。

### 3.3 例外
- EBase（R2 の CP0 15 sel 1）を kernel の `.text` の 4 KiB に揃えた symbol に向け、vector を copy しない（R1・MIPS IV（Qube の RM5230）は EBase が無く `0x80000000` に copy が要る、その時に足す）。
- Status.BEV を 0 にするのは EBase の設定の後。Cause.IV = 0（24Kf に VInt が無い）なので割り込みも +0x180 の general に来る。
- k0・k1 は例外の入口専用。入口: Status.KSU（UM）を見て user からなら percpu の `kernel_sp`（UP なので固定の KSEG0 の変数、SMP の時は KScratch か Context の PTEBase の bit）へ切り替える。
- trap frame（MIPS32）: 32 個の GPR ＋ hi・lo・EPC・Status・Cause・BadVAddr = 38 word = 152 byte（8 byte に揃えて 152）。MIPS64 は 38 × 8 = 304 byte。
- 分岐の遅延 slot: Cause.BD が 1 の時の fault は EPC が分岐を指す。user の emulation（FPU の命令の emulation 等）は作らないので、signal の時の EPC はそのまま。
- system call は `syscall`。HAL の約束（今回は kernel の self-test だけ）: 番号 v0、引数 a0〜a3・t0・t1（o32 では 5・6 番目の引数を stack から写す形もあるが、user の ABI は userland の WS で決める）、戻り値 v0、error の印 a3。EPC は +4 してから戻る。

### 3.4 TLB と page table
- page は 4 KiB、EntryHi の VPN2 は 8 KiB の対（偶・奇）。MIPS32 の table は 2 段（10 + 10 + 12、i386 と同じ形）、葉の PTE は 32 bit。
- PTE の形: bit 0 G、1 V、2 D、3〜5 C（EntryLo と同じ）、6〜29 PFN（物理 >> 12）、30〜31 software（30 = accessed は持たない、31 = 「書いてよい」）。refill は PTE を EntryLo0/1 にそのまま（`ext` で bit 0〜29 を取る）入れる。
- refill（EBase+0x000、k0・k1 だけを使う）: BadVAddr → 現在の space の root（UP は固定の KSEG0 の変数）→ `root[va >> 22]` が 0 なら general の例外へ（`j` で TLBL/TLBS の扱い）→ 偶・奇の 2 つの PTE を EntryLo0/1 に → `tlbwr` → `eret`。
- dirty の追跡: page は D = 0 で入れ、最初の書き込みの TLB Modified（ExcCode 1）で software の「書いてよい」を見て D を立て、その entry を `tlbp`・`tlbwi` で書き直す（ppc32 の hashed table と同じ考え）。
- ASID は 8 bit（EntryHi）。space ごとに割り当て、使い切ったら全 TLB を flush して世代を進める。kernel の entry は wired（Wired register）に置かない（KSEG0/1 を使うので kernel の entry は要らない）。
- TLB の entry 数は Config1.MMU + 1 から（24Kf 16、MIPS64R2-generic 64）。初期化は全 entry を KSEG0 の互いに違う VPN（EntryHi に `0x80000000 + i * 0x2000`、V=0）で書いて machine check を避ける。

### 3.5 cache
- QEMU は cache を模さない（cache 命令は何もしない）が、実機（RG350・CI20・Qube）のために正しく書く。Config1 の IL/IS/IA・DL/DS/DA から line・set・way を求める。
- Config.K0 = 3（cacheable noncoherent、write back）を入口で KSEG1 から設定する（入口の値は未確認、§1.6）。
- API の写像: D cache の書き出し → `cache Hit_Writeback_Inv_D`（範囲を line ごと）、命令の同期 → R2 の `synci`（`rdhwr SYNCI_Step`）と `sync`・`jr.hb`。DMA の範囲は書き出し・無効化。user の page を実行可能にする時に命令の同期（arm64 の WS044 p009 と同じ規則）。

### 3.6 timer
- CP0 Count（160 MHz、§1.1）と Compare。tick は 1000 Hz（`HAL_TIMER_FREQUENCY`、riscv32 と同じ）、Compare に次の値を書くと IP7 が落ちる。遅れた時は Count から次の境界を求め直す（Compare を過ぎた値に書くと 2^32 cycle 来ない）。
- 64 bit の monotonic の counter は Count の wrap（160 MHz で約 26.8 秒）を tick の度に数えて作る（tick が 26 秒止まらない前提、idle の `wait` でも Compare の割り込みで起きる）。
- idle は `wait`。

### 3.7 atomic
- MIPS32: `ll`/`sc` の 32 bit だけで、64 bit の操作が無い → `HAL_ATOMIC_STYLE_ENTER`（riscv32・ppc32 と同じ）、`hal_atomic_uint_try_acquire` は `ll`/`sc` の swap。
- MIPS64: `lld`/`scd` があるので riscv64 と同じ native の style（§8）。
- `sync` を acquire・release の barrier に使う（R2 の軽い sync の種類は使わない）。

## 4. HAL の API の差分（[proposed/hal-mips.md](proposed/hal-mips.md)、**適用しない、承認待ち**）

1. `include/hal/arch.h` に `HAL_ARCH_MIPS32`（`#include <hal/arch/mips32.h>`）と `HAL_ARCH_MIPS64`（`mips64.h`）の節。
2. `include/hal/arch/mips32.h` を新規（riscv32.h と同じ節の並び: tick 1000 Hz、signal frame の head 16 byte（token・reserved・context_pointer・padding）、atomic は enter の style と `ll`/`sc` の `hal_atomic_uint_try_acquire`・spin の hint（`pause` は R2 の MT だけなので `nop` を並べる、*）、`hal_gpregs` は 32 bit の r[32]・hi・lo・pc・status・cause・badvaddr、`hal_fpregs` は 64 bit の f[32]・fcsr、debug point なし（Watch register は後で）、checks）。
3. `include/hal/arch/mips64.h` を新規（同じ並び、64 bit の register、atomic は native、signal frame の head は riscv64 と同じ 16 byte）。
4. `hal_get_arch_handoff` の名前付きの値: `boot.command-line`（既存の名前）、`mips.initrd`（物理の範囲）。memory の大きさは汎用の `kern_boot_handoff` の memory map で渡すので名前の値にしない。
- hal.h・`include/kern/boot.h` は変えない。
- **承認の範囲**: 2026-10-18 の「新しい port の事前の承認（形を限る）」は文面が WS263〜WS266 の port に限られる（Guardrail の表）。WS271・WS272 に同じ形を広げてよいかをユーザーに聞く（§10 の Q1）。

## 5. HAL の外の変更（共通）

- `include/uapi/stat.h`: o32 は 64 bit の member を 8 byte に揃える（*）ので riscv32・ppc32 の枝の条件に `KERN_USER_ABI_MIPS32` を足す。n64 は LP64 の既存の枝（`KERN_USER_ABI_MIPS64` を riscv64 と同じ側に）。userland が無いので ABI は未使用、Q1 の確認が要る path。
- `include/kern/elf.h` に `EM_MIPS`（8）、`src/kern/elf.c` に `HAL_ARCH_MIPS32`（ELF32、little endian）・`HAL_ARCH_MIPS64`（ELF64）の枝。
- kernel の platform の binding: arch に依らない `src/kern/platform/riscv-virt.c` を ppc32・m68k と同じく流用（名前の見直しは WS264 の課題に従う）。PCI の bus の driver は今回は作らない（VGA は HAL が直に扱う）。
- `Makefile`・`tools/menuconfig.py` に `mips32-malta`（arch `mips32`、board `malta`、build `build/mips32-malta`）・`mips64-malta`。`platform/mips32-malta/`・`platform/mips64-malta/`。`config/ci/config-mips32-malta.mk`（`plan/ws271/tests/` に同じ物、p002 で作る。形は plan/tools/ports/config-riscv32-virt.mk と同じ、driver は全部 n）。`NO_DESKTOP_ARCHITECTURES` に mips32・mips64。

## 6. 試験（共通）

- `plan/tools/boot-test.sh` に `BOOT_MODE=mips32-malta`・`mips64-malta`（**Q1 が変える**、案は [proposed/boot-test-mips.md](proposed/boot-test-mips.md)）:
  `qemu-system-mipsel -M malta -m ${BOOT_MEMORY:-256M} -vga std -display none -no-reboot -serial unix:…,server,nowait -qmp unix:…,server,nowait -kernel "$image" [-append "$BOOT_APPEND"]`。
  64 bit は `qemu-system-mips64el -M malta -cpu MIPS64R2-generic`。既存の mode の振る舞いは不変、判定は QMP の screendump の PNG。
- 判定（T1）: 画面に `MIPS32 HAL READY DISPLAY VGA PAGING PASS TIMER TICK`、`MIPS32 CPU 1 ONLINE`、`VFS initialization failed`（64 bit は `MIPS64`）。
- 解析は gdbstub（`-S -gdb tcp::<port>`、host の `gdb-multiarch`（host に有り、`set architecture mips:isa32r2` を受けると 2026-10-19 に確かめた））。

## 7. WS271（mipsel）に固有

### 7.1 link と入口
- `0x80100000`（KSEG0）に link した ELF をそのまま `-kernel` に（flat image は要らない）。物理 `0x00100000`〜。物理 `0x2000` の PROM の表は入口で HAL の bss に写してから使う（RAM の page の allocator が物理 0〜1 MiB を使わないので、写さなくても壊れないが、写す方が明確）。
- 起動の段: (1) `_start`（KSEG0）: Status を確定、Config.K0 を設定、bss を消す、stack、EBase。(2) `mips_cmain`: a0〜a3 を受け、argv・環境を写す。(3) `bsp-malta` が PCI の VGA を設定し console を出す。(4) TLB の初期化、page・space の self-test、timer、割り込み（8259 は全部 mask、timer だけ）。(5) kernel へ。
- memory は `memsize`（無ければ a3）の大きさで物理 0 から。kernel の image と物理 0〜1 MiB を除く。

### 7.2 制限
- UP。外部割り込み（8259）の source の登録は作る（PIIX4 の IDE・USB は driver が無いので使わない）。reset/poweroff は FPGA の reset register（`0x1f000500` に `0x42`、*）か止まるだけ（p002 で決める）。kernel の W^X 無し。

## 8. WS272（mips64el）の差分

| 項目 | MIPS32（WS271） | MIPS64（WS272） |
| --- | --- | --- |
| QEMU | `qemu-system-mipsel -M malta`（24Kf） | `qemu-system-mips64el -M malta -cpu MIPS64R2-generic`（MIPS64R2、FPU、TLB 64 entry、SEGBITS 42・PABITS 36、CCRes 2 → Count 160 MHz（cpu-defs.c.inc で確認、ws272-p001）。既定の 20Kc は R1 で EBase・`ext` が無いので使わない） |
| ABI | o32、`-mabi=32` | n64、`-mabi=64`、`-march=mips64r2`、`-msym32`（kernel の symbol は CKSEG0 の 32 bit の符号拡張） |
| linker | `elf32ltsmip` | `elf64ltsmip` |
| link | `0x80100000` | `0xffffffff80100000`（CKSEG0）。QEMU の `cpu_mips_kseg0_to_phys` は 64 bit でも同じ物理 `0x00100000` に置く（*） |
| 受け渡し | a0〜a3 は 32 bit | 同じ値を 64 bit に符号拡張（bootloader の `bl_gen_load_ulong` が ISA_MIPS3 で `dli`、bootloader.c で確認。`ENVP_VADDR` 自体が符号拡張の形かは *）。表の pointer は 32 bit の KSEG0 のまま（`ENVP_VADDR`）なので、表の entry は 32 bit で読み符号拡張する |
| Status | — | UX=1（user の 64 bit の address）、KX=1、SX=0、FR=1。entry では KX=0（bootloader は Status を書かない、*）なので XKPHYS に触れる前に KX を立てる |
| direct map | KSEG0（256 MiB まで） | XKPHYS（`0x9800000000000000 + P` cached、`0x9000000000000000 + P` uncached）で RAM の全体（物理 `0x80000000` の 2 GiB）。Config3.LPA=1 だが RAM は 32 bit の物理の中なので PageGrain.ELPA は立てない。kernel の image は CKSEG0 |
| user | kuseg 2 GiB | xuseg（SEGBITS 42 = 4 TiB。user は下の 512 GiB だけ使い、3 段の table（9 + 9 + 9 + 12）、PTE 64 bit） |
| refill | EBase+0x000、`tlb32.S` | EBase+0x080 の XTLB refill、`tlb64.S`（XContext の BadVPN2 は使わず BadVAddr から歩く） |
| atomic | enter の style | native（`lld`/`scd`） |
| trap frame | 152 byte | 304 byte |
| arch の header | `mips32.h` | `mips64.h` |
| ELF | ELF32 | ELF64、`EM_MIPS` |
| int64.c | 要る | 要らない |

WS272 の p001 は、この表と §1〜§6 を読んで自分の CPU の型の事実（MIPS64R2-generic の Config、64 bit の bootloader の受け渡し）を gdb で確かめる計画を phase.md に書くだけ（LW 2）。

## 9. Phase と LW（8 LW ≈ 2〜3 時間）

### WS271
| Phase | 目的 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 | 4 | — |
| p002 | 入口・console の画面: `src/hal/mips/` の locore・asm.h・cmain、bsp-malta の pci・framebuffer・cons、build の配線（Makefile・menuconfig・platform・config）。到達点は画面の `MIPS32 HAL` の行（T1） | 12 | LLVM の Mips（Q1）か GCC の代わりの道、boot-test の mode（Q1）、HAL の API の承認（§4） |
| p003 | 例外・TLB・page・space・task・timer・FPU・atomic、VFS の段まで（T1 の画面で到達点） | 20 | p002 |
| p004 | 規約の全文の見直し | 4 | p003 |

### WS272
| Phase | 目的 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | §8 の確認の計画 | 2 | WS271 p001 |
| p002 | 64 bit の入口・console（共通の層を `HAL_ARCH_MIPS64` で build、XKPHYS、CKSEG0） | 8 | WS271 p002、LLVM の Mips |
| p003 | XTLB refill・3 段の table・native atomic、VFS の段まで | 10 | WS271 p003、WS272 p002 |
| p004 | 規約の全文の見直し | 3 | p003 |

WS271 の ws.md の Phase の表（p001〜p003）は p004 を足した 4 行に直した（p003 の規約の見直しを p004 に、p003 を「例外〜VFS」に）。

## 10. ユーザーに聞く判断

| # | 問い | 選択肢 | 既定（案） | 理由 |
| --- | --- | --- | --- | --- |
| U1 | HAL の API の差分（§4: arch.h の 2 節、mips32.h・mips64.h、handoff の名前）を、2026-10-18 の新しい port の事前の承認（形を限る）と同じ扱いにしてよいか | (a) 同じ形で事前に承認 (b) 差分ごとに見て承認 | (a) | 形が WS263〜266 と同じ。差分は proposed/ にある |
| U2 | p002 の toolchain を LLVM の Mips の target（共有の LLVM の再 build）で待つか、先に Debian の GCC の cross で始めるか | (a) LLVM を待つ (b) GCC で始め、LLVM の後に切り替え | (a) | 2026-10-19 の「toolchain 許可する（LLVM に Mips）」に沿う。GCC は WS266 の前例があるが 2 つの compiler の差（`-Wno-maybe-uninitialized` 等）が増える |
| U3 | user の hardfloat の ABI を o32 の FPXX（FR=1 の kernel で FP32 の古い binary は動かない）にしてよいか | (a) FPXX（FR=1） (b) FP32（FR=0、Debian の古い既定） (c) FP64 | (a) | 今の Debian mipsel・Linux の既定と同じ考えで、FPU の 32 個の register を全部使える。zedBSD は既存の binary を持たない |
| U4 | MIPS64 の QEMU の CPU を `MIPS64R2-generic` にしてよいか（既定の 20Kc は R1） | (a) MIPS64R2-generic (b) 5KEf (c) Octeon68XX（Octeon の実機に近い、big endian が要るかは未確認） | (a) | R2 の EBase・`ext`/`ins` を MIPS32 の HAL と共通に使える |
| U5 | 画面の大きさ | (a) 640x480（80x30） (b) 800x600 (c) 1024x768 | (a) | 他の port の 80x25〜30 と揃う。uncached の描画が軽い |

## 11. Q1 への依頼

1. toolchain: [proposed/llvm-mips.md](proposed/llvm-mips.md) の案（targets に Mips、ZedBSD の triple の case、linker の emulation、patch level）を適用・build・lock。適用の前に kernel の build（`mipsel-unknown-elf`）には Mips の target だけで足りる。
2. HAL の承認: §4（U1）をユーザーに。
3. tools: boot-test.sh の `mips32-malta`・`mips64-malta`（[proposed/boot-test-mips.md](proposed/boot-test-mips.md)）。host の `gdb-multiarch` は有る（確認済み）。
4. Guardrail・Master の Tools の登録（mode の追加の後）。
