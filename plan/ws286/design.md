<!-- awesome-plan project=zedbsd record=ws286-p001-design -->

# WS286 の設計: Digilent ZYBO（初代、Zynq-7000 XC7Z010）への移植 — ARMv7-A の HAL・toolchain・BSP・driver

2026-10-23 P1（[ws286-p001](phase001/phase.md)、worktree p1-zybo、基点 main 6206d397e）。親: [WS286](ws.md)。
手本は [WS263](../ws263/design.md)（riscv32）・[WS271](../ws271/design.md)（mips32）の「新しい arch の足し方」と、`src/hal/arm64/`（GIC・SMP・BSP の分け方）。

ユーザーの決定（2026-10-23、Q1 経由、原文）:「この計画で取り組みましょう。Fableでの設計やレビューは不要です。Switchbotは既存のもの、ZYBOのIPはシリアルで確認、UARTはホストに接続、で行きます。QEMUも、少なくともarmv7 virtとしては積極的に利用しましょう。zynqゲストが利用できるならぜひ利用したいですが、それをビルドするよりも、スキップして実機に進む方が早そうです。」
→ host の QEMU 10.0.11 には `virt` と `xilinx-zynq-a9` の両方が入っている（§1.4、build 不要）ので、HAL の立ち上げは `virt`、BSP の早い確かめは `xilinx-zynq-a9`、その後に実機（§8）。設計の review は Q1 が通常の手配をする。

## 0. 出典と確認の水準

- 読んだ tree の物: `include/hal/hal.h`（全節）、`include/hal/arch.h`・`arch/riscv32.h`・`arch/aarch64.h`・`arch/i386.h`、`src/hal/arm64/`（locore.S、int.c、irq.c、smp.c、task.c の signal、bsp.h、bsp-rpi-common/gic.c・uart.h、bsp-rpi4/uart.c・board.c）、`src/hal/i386/defs.h`、`platform/riscv32/*`・`platform/arm64/*`・`platform/riscv64/visionfive2/*`、`toolchain/llvm/llvm.mk`・`sysroot.mk`・`version.mk`・`patches/0001-add-zedbsd-x86-target.patch`、`Makefile`（platform の配線）、`tools/menuconfig.py`、`src/kern/elf.c`、`include/uapi/stat.h`、`include/kern/boot.h`・`platform.h`、`src/kern/platform/riscv-virt.c`・`rpi5.c`・`visionfive2.c`、`src/drivers/pci/pci-ehci.c`、`src/drivers/platform/rpi5/rp1-ethernet.c`、`src/drivers/platform/rpi4/rpi4-sdhci.c`、`src/drivers/platform/visionfive2/vf2-ethernet.c`、`include/drivers/ethernet/dw-eqos.h`・`mii-phy.h`・`bcm54213pe.h`、`include/drivers/usb/usb.h`、`include/drivers/generic/dma.h`、`include/drivers/serial/ns16550.h`、`src/drivers/generic/serial-console.c`、`userland/firmware/zedbsd-firmware.mk`、`platform/arm64/firmware.mk`、`plan/tools/switchbot.py`、`plan/tools/vf2/run.sh`、`plan/tools/boot-test.sh`（mode の配線）、`plan/agents/protocol.md`（T1）。
- 外の source は scratchpad `…/scratchpad/refs/`（tree に入れない、commit しない）に取り、下の表の版で読んだ。comment に引く時の形は Guardrail の「Value(s) referenced:」「Procedure referenced:」（block 単位、file の先頭には書かない）。

| 出典 | 版 | 読んだ file（SHA-256 の先頭 16 桁） |
| --- | --- | --- |
| Linux（GPL-2.0、値と手順だけ引く） | v6.12（git.kernel.org） | `arch/arm/boot/dts/xilinx/zynq-7000.dtsi`（71b9e572a111e037）、`zynq-zybo.dts`（462b47ab318dc279）、`arch/arm/mach-zynq/platsmp.c`（b63d1698715e6077）、`headsmp.S`（4790bbe689bcda91）、`slcr.c`（f6025d16af8cea47）、`common.c`（25b001846da04b01）、`drivers/tty/serial/xilinx_uartps.c`（dbef87fddbca644d）、`drivers/fpga/zynq-fpga.c`（ad19cfeb81e59aad）、`drivers/clk/zynq/clkc.c`（8c6d138910712ce2）、`drivers/mmc/host/sdhci-of-arasan.c`（320a356f586e841c）、`drivers/net/ethernet/cadence/macb_main.c`（38508b69e04345af）・`macb.h`（b5888952d0cf930e）、`drivers/usb/chipidea/core.c`（482a8c5b62e523da）・`host.c`（d4c978caa5dcd340）・`ci.h`（d4fcabdd19438ea4）・`ci_hdrc_usb2.c`（4204ba225470b4ed）、`drivers/net/phy/realtek.c`（0cb29edc739e7eff）、`drivers/watchdog/cadence_wdt.c`（6d1bf5fc814fced7）、`arch/arm/mm/proc-v7.S`（c6e5be200e574fdf）、`arch/arm/kernel/head.S`（9ed44dcfaacfa529） |
| U-Boot（GPL-2.0+） | v2025.07（raw.githubusercontent.com） | `arch/arm/dts/zynq-zybo.dts`（739e1d677a95a2fd）、`board/xilinx/zynq/zynq-zybo/ps7_init_gpl.c`（a702314f485e42a0）、`arch/arm/mach-zynq/spl.c`（5501d11d146c1e3e）・`cpu.c`（bebe93bfd80716f6）、`board/xilinx/zynq/board.c`（6d567b0bc50eb4b9）、`drivers/fpga/zynqpl.c`（4993d7eadeb12cb2）、`drivers/usb/host/ehci-zynq.c`（eb73f6a3d887af74）、`include/configs/zynq-common.h`（688bc1b6f781a565）、`Licenses/README`（79feed9f3865276a） |
| AMD embeddedsw（FSBL） | xilinx_v2024.2 | `lib/sw_apps/zynq_fsbl/src/main.c`（caed54c21a843277、SPDX MIT）、`fsbl.h`（fd46c45742f39b09）、`image_mover.c`（9bf4f081623e7cc2）、`license.txt`（12bf8dad928a5964） |
| QEMU | v10.0.11（host の Debian の版と同じ tag） | `hw/arm/xilinx_zynq.c`（cab3322dad698895）、`hw/arm/boot.c`（f2cd884f4a25b2f3）、`hw/arm/virt.c`（25ba8b095c0999cc）、`hw/misc/zynq_slcr.c`（9ff65ce3dc0aee2f）、`hw/char/cadence_uart.c`、`hw/net/cadence_gem.c`、`hw/sd/sdhci.c`、`hw/intc/arm_gic.c`、`hw/timer/a9gtimer.c`・`arm_mptimer.c` |

- UG585（Zynq-7000 TRM）は PDF を取っていない。この文書で「UG585」と書く事実は、device tree・Linux・U-Boot・QEMU の source に現れる値から確かめた物で、表に無い事（CPU1 の BootROM の待ち方の細部、USB の VBUS の配線）は **未確認** と明記した。
- 確認の水準: build・QEMU・実機は何も走らせていない（設計の Phase）。QEMU の machine と CPU の有無は `qemu-system-arm -M help`・`-cpu help` で確かめた（§1.4）。

## 1. 板と SoC の事実

### 1.1 CPU と memory map（Z-7010、ZYBO）

- 2× Cortex-A9 MPCore（ARMv7-A、VFPv3-D32・NEON、Security Extensions 有り、LPAE 無し）。SCU・GIC（PL390）・private timer/watchdog・global timer は MPCore の private 領域 `0xF8F00000`（QEMU `xilinx_zynq.c` `MPCORE_PERIPHBASE`）: SCU +0x000、GIC CPU interface +0x100（dtsi `0xF8F00100`）、global timer +0x200、private timer +0x600、watchdog +0x620、GIC distributor +0x1000（`0xF8F01000`）、L2（PL310）`0xF8F02000`。
- DDR3 512 MiB、物理 `0x00000000`〜`0x1FFFFFFF`（zybo dts `memory@0`）。OCM 256 KiB、FSBL/U-Boot の後は `0xFFFC0000`〜`0xFFFFFFFF`（U-Boot `cpu.c` `arch_cpu_init`: `ocm_cfg = 0x1F`、`filter_start = 0`）。
- PL の AXI の窓: GP0 `0x40000000`〜`0x7FFFFFFF`、GP1 `0x80000000`〜`0xBFFFFFFF`（物理。PL を書くまでは何も無い）。
- IOP: UART0 `0xE0000000`、UART1 `0xE0001000`、USB0 `0xE0002000`、USB1 `0xE0003000`、I2C `0xE0004000`/`0xE0005000`、SPI `0xE0006000`/`0xE0007000`、CAN、GPIO `0xE000A000`、GEM0 `0xE000B000`、GEM1 `0xE000C000`、QSPI `0xE000D000`、SMC `0xE000E000`、SDIO0 `0xE0100000`、SDIO1 `0xE0101000`。PS: SLCR `0xF8000000`（clkc +0x100、reset +0x200、pinctrl/MIO +0x700）、TTC `0xF8001000`/`0xF8002000`、DMAC `0xF8003000`、SWDT `0xF8005000`、DDRC `0xF8006000`、devcfg `0xF8007000`、XADC `0xF8007100`。全部 dtsi の `reg`。
- 割り込み（dtsi の `interrupts`、GIC の ID = SPI 番号 + 32）: UART1 50→82、UART0 27→59、GEM0 22→54、SDIO0 24→56、USB0 21→53、devcfg 8→40、L2 2→34、SWDT 9→41、GPIO 20→52、TTC0 10〜12→42〜44、QSPI 19→51。PPI: global timer 27、private timer 29、private watchdog 30。
- ZYBO の配線（zybo dts）: console は UART1（`serial0 = &uart1`、115200n8、MIO48/49 の USB-UART = FT2232H の B channel）、SD は SDIO0、LAN は GEM0 + RGMII-ID の PHY（MDIO address 0、RTL8211E、ID `0x001cc915` は Linux `realtek.c`）、USB0 は host・ULPI（USB3320）、PHY の reset は `gpio0 46`（active low）。QSPI は U-Boot の dts だけ `okay`。`ps-clk-frequency = 50000000`。

### 1.2 clock（ZYBO の ps7_init_gpl.c の値。QEMU の SLCR の model も同じ register を持つ）

| clock | 設定（SLCR） | 周波数 |
| --- | --- | --- |
| PS_CLK | 板の水晶 | 50 MHz |
| ARM PLL | `0xF8000100` FDIV=0x1A（26） | 1300 MHz |
| CPU（cpu_6or4x） | `0xF8000120` ARM_CLK_CTRL DIVISOR=2、SRCSEL=ARM PLL | 650 MHz |
| periphclk（private/global timer、SCU） | CPU/2（A9 MPCore の固定） | 325 MHz |
| cpu_1x（APB の pclk: UART・GEM の MDIO・SDIO の AHB） | CLK_621_TRUE=1（`0xF80001C4`）→ CPU/6 | 108.33 MHz |
| IO PLL | `0xF8000108` FDIV=0x14（20） | 1000 MHz |
| GEM0 tx | `0xF8000140` SRCSEL=IOPLL、DIV0=8、DIV1=1 | 125 MHz（1000BASE-T。100 は 25 MHz = DIV 40、10 は 2.5 MHz = DIV0 20 × DIV1 20） |
| SDIO | `0xF8000150` DIV=20 | 50 MHz（SDHCI の base clock。capability の base clock は 0 なので SLCR から取る: Linux `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN`） |
| UART | `0xF8000154` DIV=10、UART1 enable | 100 MHz（115200 = 100 MHz / (CD × (BDIV+1))、例 CD=62・BDIV=13 → 115207） |
| PCAP | `0xF8000168` DIV=5 | 200 MHz |
| FCLK0（PL） | `0xF8000170` DIV0=10 | 100 MHz |

`APER_CLK_CTRL`（`0xF800012C`）は 0x01EC044D: UART1・GEM0・SDIO0・USB0・GPIO・DMA の AMBA clock が有効（p006 で値を読んで確かめる）。QEMU の `zynq_slcr.c` は同じ register を reset 値で持つが、timer の model は SLCR を見ない（`arm_mptimer.c` `timerblock_scale`: 1 tick = (prescaler+1)×10 ns = 100 MHz 相当。`a9gtimer.c` も同じ 10 ns）。→ **§3.6 の timer の周波数は BSP が「SLCR から計算」と「QEMU では 100 MHz」を切り替える**（§1.4 の判別）。

### 1.3 起動と CPU1

- BootROM → BOOT.BIN の最初の partition（FSBL か U-Boot SPL）→ U-Boot（§4）。CPU0 だけが走り、CPU1 は BootROM の待ち loop に残る。**CPU1 の起こし方（実機）は 2 つ**:
  1. **OCM の `0xFFFFFFF0` に入口の物理 address を書き、`dsb; sev`**（BootROM が CPU1 を `wfe` で待たせ、起きたら `0xFFFFFFF0` を読んで飛ぶ、という UG585 6.3.9 の手順。**UG585 は未読なので p007 で実機で確かめる**。U-Boot・FSBL はこの loop に手を触れない）。
  2. Linux の方法（`platsmp.c` `zynq_cpun_start`、`slcr.c` `zynq_slcr_cpu_stop/start`）: SLCR を unlock（`0xF8000008` = 0xDF0D）、`A9_CPU_RST_CTRL`（`0xF8000244`）で CPU1 を reset（bit1 RST、bit5 CLKSTOP）、物理 0 に「`ldr r0, [pc, #0]; bx r0; .word 入口`」の 3 word を置いて cache を書き出し、reset を外す。1 が効かない時の fallback。
- QEMU `xilinx-zynq-a9` は `-smp 2` まで（`ZYNQ_MAX_CPUS 2`）。ELF を `-kernel` で渡すと `is_linux = 0`（`boot.c` `arm_load_elf`）で **2 つの CPU が同時に ELF の入口に入る**（`do_cpu_reset` の `!info->is_linux` の枝）。QEMU の SLCR の model は `A9_CPU_RST_CTRL` を書いても CPU を reset しない（`zynq_slcr.c` 471 行: ただの store）。DTB は渡らない（zynq の binfo に dtb が無い）、`-append` も渡らない。→ **QEMU では、CPU1 は自分で入口に来て HAL の park loop で待ち、CPU0 が release word を書いて `sev` する**（§3.7）。
- QEMU `virt`（32 bit、`-cpu cortex-a15`）: GICv2 `0x08000000`（dist）/`0x08010000`（cpu）、PL011 `0x09000000`、ARMv7 の generic timer（62.5 MHz）、virtio-mmio、DTB を `r2` で渡す（`-kernel` の ELF でも `dtb_start = loader_start` に置く）、二次 CPU は PSCI（`smc`、`CPU_ON`）で起こす。LPAE は使わない（短い descriptor で走る）。

### 1.4 QEMU の machine の有無（確認済み、2026-10-23）

`qemu-system-arm --version` = 10.0.11（Debian 1:10.0.11+ds-0+deb13u1）。`-M help` に `virt`（10.0）と `xilinx-zynq-a9`（Cortex-A9、Zynq 7000）、`-cpu help` に `cortex-a9`・`cortex-a15`・`cortex-a7`。zynq の model に有る物: SLCR、GIC（PL390 の register 互換）、private/global timer、Cadence UART ×2、Cadence GEM ×2、SDHCI ×2（capabilities 0x69ec0080）、ChipIdea ×2、QSPI、TTC、devcfg（`0xF8007000`、`hw/dma/xlnx-zynq-devcfg`）、OCM。無い物（unimplemented）: GPIO、I2C、CAN、SWDT、DDRC、SMC、L2 の PL310（QEMU は L2 を透過する。`-M xilinx-zynq-a9` に PL310 の model は無い、書き込みは落ちるか無視 → **p006 の L2 の有効化は実機だけで確かめる**）。
QEMU か実機かの判別: SLCR の `PSS_IDCODE`（`0xF8000530`）は両方で読めるので使えない。**`0xF8F02000` の PL310 の `CACHE_ID`（+0x000、上位 8 bit = 0x41）を読み、ARM の ID でなければ L2 無し・timer 100 MHz（QEMU）と判定**する（p003 で確かめる。読みが bus error になるなら SLCR の `A9_CPU_RST_CTRL` の読み戻しの差（QEMU は write-through）で判別する案に切り替える）。

## 2. toolchain と sysroot

### 2.1 LLVM（**Q1 の許可が要る toolchain の変更**。P1 は適用しない）

- 今の `toolchain/llvm/llvm.mk` 361 行: `-DLLVM_TARGETS_TO_BUILD='PowerPC;AArch64;RISCV;X86;Mips'`。**`ARM` が無い**（`--print-targets` に arm が出ない見込み。P1 は共有の `build/llvm` を走らせていない: 確認は Q1）。
- 案 1（必須、kernel の build に要るのはこれだけ）: targets に `ARM` を足す（`'PowerPC;AArch64;RISCV;X86;Mips;ARM'`）、`version.mk` の `ZEDBSD_LLVM_PATCH_LEVEL` を `zedbsd10` → `zedbsd11`（install の stamp と cache asset の名前を変えるため。memory の「LLVM patch level needs new cache」: rev-0 の cache asset の upload と digest の更新を同じ commit で）。大きさ: ARM の backend の build は AArch64 と同程度（数分〜十数分、`build/llvm-build` が数百 MB 増える）。
- 案 2（userland を作る段で要る、p009）: `0001-add-zedbsd-x86-target.patch` に同じ形で足す:
  - `clang/lib/Basic/Targets.cpp` の `case llvm::Triple::arm:`・`armeb`・`thumb`・`thumbeb` の OS の switch に `case llvm::Triple::ZedBSD: return std::make_unique<ZedBSDTargetInfo<ARMleTargetInfo>>(Triple, Opts);`（`ARMleTargetInfo`、big endian は作らない）。
  - `clang/lib/Driver/ToolChains/ZedBSD.cpp` の linker の `-m`: `arm` → `armelf_linux_eabi`（ld.lld の ARM の emulation 名）。`-z max-page-size=4096` は aarch64 と同じ枝に `arm` を足す。
  - 同じ file の ARM の既定: `-mfloat-abi=hard`、`-mfpu=neon-vfpv3`、`-mtp=cp15`（TLS を `TPIDRURO` で読む）、`-marm`（Thumb-2 は使わない、§3.9）。
  - `TripleTest.cpp`・`clang/test/Preprocessor/zedbsd.c`: `armv7-unknown-zedbsd-eabihf` で `__ZEDBSD__`・`__arm__`・`__ARM_ARCH 7`・`__ARM_PCS_VFP`。
- triple: **`armv7-unknown-zedbsd-eabihf`**（環境名 `eabihf` で hard-float を名乗る。`arm-unknown-zedbsd` に `-mfloat-abi=hard` を付ける案は、triple から ABI が読めないので採らない）。

### 2.2 kernel の build（platform/armv7、案 1 だけで足りる）

`--target=armv7-unknown-none-eabi -D__ZEDBSD__=1 -march=armv7-a -mcpu=cortex-a9 -marm -mfloat-abi=soft -mfpu=none -mno-unaligned-access -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror`、`ld.lld -m armelf`。
- kernel は浮動小数点を使わないので soft（VFP の register は user の物。HAL の保存・復元の asm だけ `.fpu neon-vfpv3`）。
- `-mno-unaligned-access`: SCTLR.A=0 でも device memory の非整列 access は abort になるので、kernel の code では非整列を生成させない。
- 64 bit の除算・shift・`__aeabi_*` の helper: riscv32・mips32 と同じく `src/libc/int64.c` を LTO 無しで入れる。**ARM の EABI は `__aeabi_uldivmod`・`__aeabi_ldivmod`・`__aeabi_llsl` などの別名を要求する**ので、int64.c に `HAL_ARCH_ARMV7` の時だけの `__aeabi_*` の wrapper（`__aeabi_uldivmod` は商を r0:r1・余りを r2:r3 で返す **asm** の wrapper が要る、C では書けない）を足す。p002 の最初に小さな object で未定義 symbol が無い事を `llvm-nm -u` で確かめる。
- LTO（Makefile の既定）: `-mcpu` と `-mfloat-abi` は module の属性で残るので riscv32 と同じで通る見込み。`.ARM.exidx`・`.ARM.attributes` は linker script で `/DISCARD/`（exidx）と保持（attributes、ld.lld が要る）。
- vmunix.ld: link は仮想 `0x80100000`（物理 `0x00100000`、§3.2）。`vmunix.bin`（`llvm-objcopy -O binary`）と U-Boot の legacy uImage `uvmunix`（64 byte の header + CRC32、`platform/armv7/tools/make-uimage.py`、load=entry=0x00100000、arch ARM、type kernel、comp none）を作る。QEMU は ELF、実機は uImage。

### 2.3 sysroot と libc（`toolchain/llvm/sysroot.mk`、**Q1 の変更**、p009）

`ZEDBSD_BUILD_SYSROOT` の呼び出しを 1 つ足す: `(…,armv7,armv7-unknown-zedbsd-eabihf,-march=armv7-a -mcpu=cortex-a9 -marm -mfloat-abi=hard -mfpu=neon-vfpv3 -mtp=cp15 -mno-unaligned-access,HAL_ARCH_ARMV7,src/libc/crt/crt0-armv7.S,src/libc/crt/crt1-armv7.S,armv7,user.ld,ARM,-DKERN_USER_ABI_ARMV7,$(ZEDBSD_SYSROOT_ARMV7_LIBC_SOURCES),,$(ZEDBSD_SYSROOT_ARMV7_COMPILER_RT_SOURCES),$(ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_SOURCES))`。
- `crt0-armv7.S`・`crt1-armv7.S`（aarch64 の写しを AAPCS に: `_start` は sp から argc/argv/envp、`__syscall6` は r7 = 番号・`svc #0`、`__signal_restorer` は sigreturn の syscall）、`setjmp-armv7.S`（r4–r11・sp・lr と d8–d15）。
- long double は AAPCS で 64 bit（double と同じ）なので `softfloat128.c` は要らない。hard-float なので `softfloat.c` も要らない。
- compiler-rt の builtins: 整数の共通の表に加えて **ARM の `__aeabi_*`**（`compiler-rt/lib/builtins/arm/aeabi_uldivmod.S`・`aeabi_ldivmod.S`・`aeabi_idivmod.S`・`aeabi_uidivmod.S`・`aeabi_memcpy.S`・`aeabi_memmove.S`・`aeabi_memset.S`・`aeabi_memclr.S`・`aeabi_dcmp.S`・`aeabi_fcmp.S`、`clear_cache.c`（`__clear_cache`: ARMv7 は user で `ic`/`dc` ができないので kernel の cacheflush の syscall が要る → **§3.13 の `cacheflush` syscall**。`aeabi_*` の list は p009 で `llvm-nm -u` の結果から確定する）。sysroot の名前は `build/armv7/sysroot`。
- ELF: `include/kern/elf.h` に `EM_ARM`（40）、`src/kern/elf.c` に `HAL_ARCH_ARMV7`（ELF32、little endian、`EF_ARM_ABI_FLOAT_HARD`（0x400）の無い実行 file は拒む）。
- `include/uapi/stat.h`: AAPCS は 64 bit の member を 8 byte に揃えるので `KERN_USER_ABI_RISCV32 || PPC32 || MIPS32` の枝に `KERN_USER_ABI_ARMV7` を足す（timespec 16 byte・stat 92 byte）。
- userland の `platform/armv7/user.ld`: arm64 の写し、`. = 0x00400000`、`.ARM.exidx` は `/DISCARD/`（例外の unwind は C++ の WS で見直す）。

### 2.4 大きさの見積もり

LLVM の作り直し（Q1、案 1）: build の時間は host の並列度次第で 20〜60 分。tree の差分は llvm.mk 1 行 + version.mk 2 行 + cache の digest。案 2 は patch に 60〜80 行。sysroot は sysroot.mk に 10 行 + crt0/crt1/setjmp の 3 file（計 150 行）。

## 3. ARMv7-A の HAL（`src/hal/armv7/`）

### 3.1 directory

```
src/hal/armv7/
  locore.S        入口、CPU の初期化、page table の初期化、MMU on、二次 CPU の入口と park
  vectors.S       例外 vector と stub（SRS/CPS/RFE で統一 frame）
  dispatch.S      context switch、user への戻り、VFP の保存・復元
  cmain.c         C の入口: BSP の console、memory、GIC、timer、SMP、kernel_entry()
  int.c           例外の分類（DFSR/IFSR）、kernel_*_fault_handler の呼び出し、syscall
  irq.c           hal_irq_*（GIC の上）
  gic.c/.h        GICv1/v2 の distributor と CPU interface（arm64 の bsp-rpi-common/gic.c を 32 bit に直した写し）
  clock.c/.h      tick と counter: BSP の ops（§3.6）
  smp.c           CPU の lifecycle、IPI（SGI）、hal_cpu_*
  page.c/pte.h    短い descriptor の page table、kernel の L1、L2 の table の割り当て
  space.c         hal_space_*（ASID、TTBR0、TLB の broadcast）
  cache.c         hal_dcache_*・icache・PL310 の outer の操作
  task.c          hal_task_*（frame、fork/exec、signal、TLS、debug の register）
  fpu.c/fpregs.S  VFP/NEON の lazy な切り替え
  atomic.c        （NATIVE なので空に近い: hal_atomic_relax の `yield`）
  percpu.c/.h     TPIDRPRW で per-CPU
  debug.c         register dump、panic の停止
  lib.c           hal_memset/memcpy/printf（riscv32 の写し、%l は 32 bit）
  asm.h/defs.h    cp15 の accessor、定数
  bsp.h           BSP の契約（§3.14）
  bsp-virt/       QEMU virt: board.c（FDT）、uart.c（PL011）、psci.c、timer.c（generic timer）
  bsp-zybo/       ZYBO: board.c（固定の address、DTB は任意）、uart.c（Cadence、early console）、slcr.c、timer.c（private/global timer）、l2.c（PL310）、cpu1.c（OCM/SLCR の起こし方）
```

riscv32 と同じく **arm64 の file を変えず**に写す（gic.c、BSP の分け方、lib.c）。共通化は後の課題（Future Work に書く）。

### 3.2 address の配置（短い descriptor、TTBCR.N = 1）

| 仮想 | 物理 | 用途 | descriptor |
| --- | --- | --- | --- |
| `0x00001000`〜`0x7FFFFFFF` | user の page | user（TTBR0、2 GiB、space ごとの L1 は 2048 entry = 8 KiB、8 KiB 揃え） | 4 KiB の small page（L2 の table は 256 entry = 1 KiB、4 つを 1 page にまとめて割る） |
| `0x80000000`〜`0xBFFFFFFF` | `0x00000000`〜`0x3FFFFFFF` | **direct map**（DDR。ZYBO は 512 MiB、Z-7020 の 1 GiB まで）、kernel image は `0x80100000` に link | 1 MiB の section、Normal WBWA、Shareable。image の text は section を L2 に割って RX、rodata R、残り RW（W^X を boot で確かめる、riscv32 と同じ） |
| `0xC0000000`〜`0xDFFFFFFF` | 任意 | **device 窓**: `hal_space_map_device`、`hal_pmem_map_uncached`（512 MiB） | 4 KiB の page、L2 の table は要る時に pmem から割る（kernel の L1 は 1 つなので user の L1 の写しに届く問題は無い: TTBR1 が担うため） |
| `0xE0000000`〜`0xFFFFFFFF` | 同じ（identity） | PS の IOP・SLCR・MPCore・OCM・QSPI linear（zybo）。virt は `0x08000000`〜`0x0A000000` の device を device 窓に張る | 1 MiB の section、Device（TEX=0,C=0,B=1）。OCM（`0xFFFC0000`）は Strongly-ordered（CPU1 の release word） |

- TTBR1 は kernel の L1（16 KiB 揃え、上半分の 2048 entry だけ使う）。TTBCR.N=1 で `0x80000000` 以上が TTBR1。user の L1 は 8 KiB（N=1 の TTBR0 は 2048 entry）。
- TEX remap は使わない（SCTLR.TRE=0）。属性: Normal WBWA = TEX=001,C=1,B=1、Normal non-cacheable（uncached の DMA の buffer）= TEX=001,C=0,B=0、Device = TEX=000,C=0,B=1、Strongly-ordered = 全部 0。`HAL_SPACE_WC` は Normal non-cacheable に落とす。
- SCTLR.AFE = 1（Access Flag を使う）、SCTLR.A = 0、SCTLR.V = 0 + VBAR（§3.3）。
- page table の walk は cacheable（TTBR の IRGN=01・RGN=01・S=1）。entry を書いた後は `dsb` だけで足り、clean は要らない（A9 は walk が L1 を見る）。
- `hal_space_get_page_size(1)` = 4096、`(2)` = 1 MiB（section。user には使わない）。`hal_space_get_user_range` = `0x00001000`〜`0x80000000`。

### 3.3 例外

- VBAR（cp15 c12）に kernel の vector table（Security Extensions の有る A9 で使える。virt の A15 も）。
- 全部の例外を **SVC mode の統一 frame** で扱う: 各 stub は `srsdb sp!, #0x13`（戻り pc と spsr を SVC の stack へ）、`cps #0x13`、`push {r0-r12, lr}`、`stmdb sp, {sp, lr}^`（user の sp・lr）。frame は 18 word = 72 byte（r0–r12、sp_usr、lr_usr、lr_svc、pc、spsr）。user からの例外は task の kernel stack の上に、kernel からの例外は今の SVC stack の上に積む。IRQ・abort の mode の stack は使わない（mode の stack pointer は boot で小さな per-CPU の予備にだけ向ける）。
- 分類（`int.c`）:

| 例外 | 原因 | HAL_TRAP |
| --- | --- | --- |
| Data Abort | DFSR の FS[4,3:0]（短い descriptor の符号）。translation `0b00101`/`0b00111`、access flag `0b00011`/`0b00110`、permission `0b01101`/`0b01111` → `PAGE_FAULT`（mode は DFSR.WnR（bit 11）で `WRITE`/`READ`、address は DFAR）。alignment `0b00001` → `ALIGNMENT`。external abort（`0b01000`、`0b10110`）→ `MACHINE_CHECK`。debug event `0b00010` → `DEBUG_POINT` |
| Prefetch Abort | IFSR の FS、address は IFAR、mode `EXEC` → `PAGE_FAULT` / `DEBUG_POINT`（BKPT は IFSR の debug event） |
| Undefined | VFP の命令で FPEXC.EN=0 → §3.10 の lazy の復元（kernel に知らせない）。それ以外 → `ILLEGAL_INSN`。`bkpt` → `BREAKPOINT` |
| SVC | syscall: 番号は r7、引数 r0–r5（`HAL_SYSCALL_ARGS` = 6）、戻り r0。kernel からの `svc` は無い（`HAL_FATAL`） |
| IRQ | GIC の ack → `hal_irq_*` の handler、timer の PPI は `kernel_timer_handler`、SGI は `kernel_cpu_notify_handler` / 停止 |
| FIQ | 使わない（CPSR.F は立てたまま。GIC の Group 0 を IRQ に出す: GICC_CTLR.FIQEn=0） |

- `kernel_user_return_handler()` は user への戻りの直前に IRQ を止めて呼ぶ（arm64 の `dispatch.S` と同じ形）。
- access flag と dirty の追跡（A9 は hardware で立てないので software）: `HAL_SPACE_PAGE_ACCESSED` = AF、`HAL_SPACE_PAGE_DIRTY` は **writable の page を AP で read-only に置き、最初の write の permission fault で dirty を立てて write を許す**（mips の M1〜M3 と同じ。HAL の中で解決し、kernel の fault handler には出さない）。`hal_space_prot_query` で write を外す時: entry を書き換え → `dsb` → `TLBIMVAIS`（§3.5）→ `dsb` の後に、古い entry 経由の store は完了している（ARMv7 の TLB の broadcast の完了は `dsb` が保証）ので dirty を読んで返す。

### 3.4 GIC（PL390 = GICv1、virt の GICv2 と register は同じ）

arm64 の `gic.c` の写し（`rpi_gic_*` → `armv7_gic_*`）。違い: `ARM64_DIRECT_BASE` でなく識別の address（zybo は identity の `0xF8F01000`/`0xF8F00100`、virt は FDT の `reg` を device 窓に張る）、`dsb ish` は `dsb` の `ish` 付き（ARMv7 でも `dsb ish` は有る）、spin lock は §3.8。CPU は secure state（BootROM→U-Boot→kernel、TrustZone の切り替え無し。QEMU の zynq も secure で入る）なので Group 0 が IRQ で届く（`GICC_CTLR` = EnableGrp0|EnableGrp1、FIQEn=0、AckCtl=1）。`hal_irq_set_affinity` は `ITARGETSR` で CPU1 に振れる（既定は全部 CPU0、ws208 の方針と同じ）。MSI は `HAL_ERR_UNSUPPORTED`。

### 3.5 TLB と ASID

- ASID 8 bit（CONTEXTIDR[7:0]）。space ごとに ASID と世代を持ち、255 個を使い切ったら世代を上げて `TLBIALLIS`。
- space の切り替え（`hal_space_switch`）は ARMv7 の決まった順: CONTEXTIDR に予約の ASID 0 → `isb` → TTBR0 → `isb` → CONTEXTIDR に新しい ASID → `isb`（切り替えの途中で古い ASID の entry が新しい table と混ざる事を避ける。Procedure referenced: Linux v6.12 `arch/arm/mm/proc-v7-2level.S` `cpu_v7_switch_mm` は読んでいない — p004 で読んで手順を確かめる）。
- A9 MPCore は `ACTLR.SMP=1`・`ACTLR.FW=1` で TLB・cache の maintenance を他の CPU に broadcast する → `hal_space_flush_tlb(_range)` は `TLBIMVAIS`（ASID 付き）/`TLBIASIDIS`、kernel は `TLBIMVAAIS`、その後 `dsb; isb`。IPI の shootdown は要らない（virt の A15 も同じ）。
- 二次 CPU の起動前に `ACTLR.SMP` を立てる（MMU on の前、Linux `proc-v7.S` `__v7_ca9mp_setup` と同じ順）。

### 3.6 timer（BSP の ops）

`struct armv7_clock_ops { void (*init_boot)(void); void (*init_cpu)(void); bool (*read_counter)(uint64_t *c, uint64_t *hz); void (*ack_tick)(void); uint32_t tick_irq; }`。

- zybo: tick は **private timer**（`0xF8F00600`、PPI 29、periodic、load = periphclk/1000 − 1、prescaler 0）、`hal_rtc_read_counter` は **global timer**（`0xF8F00200`、64 bit、上→下→上の読み直し、prescaler 0）。periphclk = CPU/2 を SLCR（ARM PLL の FDIV × PS_CLK / ARM_CLK_CTRL の DIVISOR / 2）から計算。QEMU と判定した時（§1.4）は 100 MHz。global timer の 64 bit は boot で 0 に揃えて走らせ、両 CPU で同じ値（1 つの counter）。
- virt: tick は generic timer の CNTP（`CNTP_TVAL`、PPI 30、FDT `arm,armv7-timer`）、counter は `CNTPCT`（`CNTFRQ` = 62.5 MHz）。
- `HAL_TIMER_FREQUENCY` = 1000（`include/hal/arch/armv7.h`）。`hal_rtc_read_epoch_time` は false（RTC 無し。時刻は networkd の NTP か `date` で）。
- `hal_cpu_idle` = `wfi`（IRQ を enable して）。`hal_cpu_idle_suspend` は `idle-suspend-unsupported.c`。

### 3.7 SMP（CPU1）

- `locore.S` の `_start`: `cpsid if`、SVC mode、MPIDR の Aff0 ≠ 0 なら **park**（§1.3: QEMU の ELF boot と、実機の fallback の両方で使う）: `wfe` → release word（`.data` の `armv7_secondary_release[cpu]`、uncached に読むため MMU off の間は物理で）を読み、0 でなければ飛ぶ。
- CPU0 の `hal_cpu_start_others()`（`smp.c`、BSP の `bsp_cpu_release(cpu, entry_phys)`）:
  - zybo: (1) OCM `0xFFFFFFF0` に `armv7_secondary_entry` の物理を書き（Strongly-ordered の alias で）、`dsb; sev`。(2) 同時に release word も書く（QEMU の park loop 用）。(3) 100 ms 待って `armv7_secondary_boot[cpu].online` が立たなければ Linux の方法（§1.3 の 2: SLCR unlock、`A9_CPU_RST_CTRL` の bit1|bit5 を立てる、物理 0 に trampoline、clean、bit を落とす）をもう一度試し、それでも駄目なら CPU1 無しで続ける（`hal_cpu_count()` = 1、画面に `ARMV7 CPU 1 ABSENT`）。
  - virt: PSCI `CPU_ON`（`smc #0`、function `0x84000003`、MPIDR、entry、context）。
- 二次 CPU の入口 `armv7_secondary_entry`: MMU off で SCTLR・ACTLR.SMP・TTBR・TTBCR・DACR を CPU0 と同じに、MMU on、per-CPU の stack と TPIDRPRW、GIC の CPU interface（`armv7_gic_init_cpu`）、timer の `init_cpu`、`hal_task_create_for_init_context()`、`kernel_secondary_entry(cpu)`。arm64 の `arm64_secondary_main` と同じ順。
- IPI: SGI 0 = notify（`kernel_cpu_notify_handler`）、SGI 1 = stop（`hal_cpu_park`・`hal_cpu_panic_all`）。`hal_task_transfer` は arm64 と同じ。
- SCU: CPU0 が `SCU_CTRL`（`0xF8F00000`、bit0 enable）を立てる（U-Boot が立てている事が多いが読んで確かめる）。`SCU_INVALIDATE_ALL`（+0x0C）で CPU1 の tag を invalidate してから起こす。

### 3.8 atomic・barrier・spin

- `HAL_ATOMIC_STYLE_NATIVE`: ARMv7-A の `ldrex/strex`、64 bit は `ldrexd/strexd`（A9 は持つ。clang は `-march=armv7-a` で 64 bit の `__atomic` を inline にする見込み。**p002 で `llvm-nm -u` に `__atomic_*`・`__sync_*` が出ない事を確かめ、出たら `HAL_ATOMIC_STYLE_ENTER` に切り替える**（i386・riscv32 の形））。
- `hal_atomic_uint_try_acquire`: `ldrex/strex` + `dmb`。`hal_atomic_relax`: `yield`。
- `hal_mb` = `dmb ish`、`hal_rmb`/`hal_wmb` = `dmb ish`（ARMv7 に ld/st の片側は無い）、`hal_io_*mb` = `dsb`（device の順序は `dsb`）。
- spin lock（`spin.h`）: arm64 の `arm64_spin_lock` の写し（IRQ を止めて取る、`wfe`/`sev` は使わず `yield`）。

### 3.9 user/kernel の ABI

- AAPCS、**EABI hard-float**（`armv7-unknown-zedbsd-eabihf`）、ARM mode だけ（Thumb-2 は作らない: user の code は `-marm`。user が Thumb に入っても CPSR.T を frame の spsr で保つので動くが、signal の handler の入口は ARM とみなす（`spsr.T = 0`）。Thumb の binary の支援は Future Work）。
- syscall: `svc #0`、r7 = 番号、r0–r5 = 引数、r0 = 戻り（`intptr_t` 32 bit。64 bit を返す syscall は i386 と同じ扱い）。
- TLS: `TPIDRURO`（user が `mrc p15, 0, r0, c13, c0, 3` で読む。`-mtp=cp15`）。`hal_task_set_tls` は task の値を保ち、switch で書く。`TPIDRURW` は user に自由（保存・復元する）。`TPIDRPRW` は kernel の per-CPU。
- stack: 8 byte 揃え（AAPCS の public interface）。`HAL_TASK_SIGNAL_FRAME_ALIGNMENT` は 16（riscv32 と同じ、強い方）。
- signal frame（`include/hal/arch/armv7.h`、riscv32 と同じ head 16 byte: token・reserved・context_pointer・padding。`HAS_RESTORER`=0、`HAS_SIGNO`=0、`HAS_INFO_POINTER`=0）: `hal_task_signal_enter` は frame を task の `signal_frame[depth]`（18 word）と VFP（`signal_fpregs`、d0–d31 + FPSCR）に保ち、r0 = signo、r1 = siginfo、r2 = ucontext、**lr = restorer**、sp = stack、pc = handler、spsr = user・ARM・IRQ enable。`hal_task_signal_return(token)` は token が一致する深さの frame を戻す（arm64 の `task.c` 416〜 と同じ形）。
- `struct hal_gpregs { uint32_t r[13]; uint32_t sp; uint32_t lr; uint32_t pc; uint32_t cpsr; uint32_t tpidruro; }`、`struct hal_fpregs { uint64_t d[32]; uint32_t fpscr; uint32_t fpexc; }`、`struct hal_vregs { uint32_t reserved; }`（NEON の register は d0–d31 と同じ物なので vregs は空）。`HAL_DEBUG_HAS_SINGLE_STEP` = 0（ARMv7 に hardware の single step は無い。Linux の arm32 も無い）、`HAL_DEBUG_POINT_MAX` = 1 で全部 0（A9 の DBGBCR/DBGWCR の利用は後の Phase）。
- `hal_task_exec_validate`: entry が user の範囲で 4 byte 揃え（ARM）、stack が 8 byte 揃え。

### 3.10 VFP/NEON の lazy な切り替え

- boot で CPACR の cp10/cp11 を full access、NSACR は secure なので不要。`FPEXC.EN` は **0 で始め**、user が VFP/NEON の命令を使うと Undefined → `fpu.c` が「この CPU の owner」を見て、別の task なら前の owner の d0–d31・FPSCR を保存し、今の task の物を復元、`FPEXC.EN=1` で戻る。switch では `FPEXC.EN` を落とすだけ（owner は per-CPU）。`hal_task_transfer` で CPU を跨ぐ時は元の CPU で保存する（arm64 の watch と同じ扱い。mips の設計の lazy と同じ）。
- FPSCR の消毒: 書く前に例外の cumulative bit（FPSCR[4:0]）と trap enable（[12:8]、A9 の VFPv3 は trap を持たない = 書いても 0）を整える。`FPEXC.EX`/`DEX`（subarchitecture の例外、A9 の VFPv3 では denormal の support code 無し → `FPEXC.DEX` が立った Undefined は `SIGFPE`）。
- kernel は VFP を使わない（`-mfloat-abi=soft -mfpu=none`）ので、割り込みの中で VFP の状態は壊れない。

### 3.11 cache（`cache.c`）

- L1: `hal_dcache_clean_range` = `DCCMVAC` の loop（32 byte line、`CTR` から取る）+ `dsb`、invalidate = `DCIMVAC`、clean+invalidate = `DCCIMVAC`、icache = `ICIMVAU` + `BPIALL` + `dsb; isb`、`hal_sync_instruction_stream` = clean + icache invalidate。
- L2（PL310、zybo だけ、§1.4 で有ると判った時）: **A9 は L1 の操作を L2 に伝えない**ので、`hal_dcache_*` は L1 の後に **PL310 の物理 address の操作**（`CLEAN_PA` +0x7B0、`INV_PA` +0x770、`CLEAN_INV_PA` +0x7F0、各 32 byte、最後に `CACHE_SYNC` +0x730）を行う。仮想→物理は direct map（引く）か、device 窓は page table を引く（`hal_dcache_*` の引数は direct map か uncached の alias で来る前提。uncached の alias は L2 も通らない（Normal non-cacheable は PL310 を bypass）ので何もしない）。
- PL310 の初期化（p006、実機だけ）: U-Boot の状態（有効かどうか）を `CONTROL`（+0x100）で読む。無効なら: SLCR `L2C_RAM`（`0xF8000A1C`）= `0x00020202`（AR#54190、Linux `slcr.c` の `SLCR_L2C_RAM` と U-Boot の両方が書く値）、`TAG_RAM_CONTROL`（+0x108）/`DATA_RAM_CONTROL`（+0x10C）の latency（値は Linux の zynq の dts `arm,tag-latency = <2 2 2>`・`arm,data-latency = <3 2 2>`: dtsi の L2 node、p006 で読んで引く）、`AUX_CONTROL`（+0x104）に way size・prefetch・early BRESP、`INV_WAY`（+0x77C）= 0xFF を待つ、`CONTROL` = 1。**有効にするのは p006 の最後の 1 step で、それまでの Phase は L2 off で走る**（U-Boot は zynq で L2 を off にして kernel に渡す見込み。p004 で読んで確かめる）。
- QEMU の zynq は L2 の model が無い: 操作は書かない（§1.4 の判定で `l2_present = 0`）。

### 3.12 memory の管理（`hal_pmem_*`）

- RAM の範囲: zybo は固定 512 MiB（DTB が有ればその `memory` を優先）、virt は FDT の `memory`。予約: 物理 `0x00000000`〜`0x000FFFFF`（CPU1 の trampoline、U-Boot の名残）、kernel image、boot の page table、U-Boot の DTB（読み終えたら解放）。`hal_pmem_to_kernel(p)` = `p + 0x80000000`（RAM の範囲だけ）。`hal_pmem_alloc_limited` の 32 bit は常に満たす。
- `hal_pmem_map_uncached`: device 窓に Normal non-cacheable で張り、張る前に L1+L2 を clean+invalidate（§3.11）。Zynq の PS の DMA master（GEM・SDIO・USB・devcfg）は CPU の cache と **coherent でない**（ACP は PL 用）ので、driver の descriptor は全部これで取る（rpi4・VF2 と同じ）。`drv_dma_constraints.coherent = 0`、`address_bits = 32`。

### 3.13 HAL の契約との対応（hal.h は変えない）

| hal.h の群 | ARMv7 での実装 | 備考 |
| --- | --- | --- |
| console `hal_putc/puts/printf` | BSP の UART（early console）、`kernel_putc` が出たら委ねる | zybo: Cadence UART1、virt: PL011 |
| `hal_cpu_*` | §3.7。`hal_cpu_count` は 2 か 1 | `hal_cpu_notify_mask` は SGI の target mask |
| `hal_irq_*` | §3.4。`hal_irq_set_wake/suspend/resume` は arm64 と同じ（mask の保存） | MSI は unsupported |
| `hal_rtc_*` | §3.6 | epoch は false |
| `hal_pmem_*` | §3.12 | |
| `hal_space_*` | §3.2・§3.3・§3.5 | `HAL_SPACE_DEVICE` の user mapping は Device 属性で許す（prot の変更は属性を保つ） |
| `hal_task_*` | §3.9・§3.10。debug の register は gpregs/fpregs、vregs は `HAL_ERR_UNSUPPORTED`、single step は unsupported、debug point は空 | |
| barrier・cache・mmio | §3.8・§3.11。`hal_io_inp/outp` は `HAL_FATAL`（port I/O 無し）、`hal_mmio_*64` は 2 回の 32 bit | |
| `hal_get_arch_handoff` | `boot.command-line`（DTB の `/chosen/bootargs`、無ければ NULL）、`armv7.fdt`（物理の範囲、riscv32 の形）、`armv7.boot-cpu` | 名前付きの値は arm64・riscv32 と同じ形 |
| `hal_reset` | zybo: SLCR unlock → `REBOOT_STATUS` の上位 bit を消す → `PSS_RST_CTRL`（`0xF8000200`）= 1（Linux `zynq_slcr_system_restart`）。virt: PSCI `SYSTEM_RESET` | |
| `hal_poweroff` | zybo: unsupported（電源は SwitchBot）、virt: PSCI `SYSTEM_OFF` | |
| `hal_entropy_fill` | global timer の下位 bit と XADC（後）。最初は counter だけ（弱い、`kern` 側の pool が混ぜる） | |
| `hal_get_memstat` | arm64 と同じ | |

**hal.h を変える必要は無い**。事前承認の形（WS263〜266・271 と同じ、Guardrail 2026-10-18 の行）で足す物:
1. `include/hal/arch.h` に `HAL_ARCH_ARMV7` の節（`#include <hal/arch/armv7.h>`）。
2. `include/hal/arch/armv7.h` を新規（§3.9 の値。riscv32.h と同じ節の並び）。
3. `hal_get_arch_handoff` の名前付きの値 `armv7.fdt`・`armv7.boot-cpu`（上の表）。
提案する差分は [proposed/hal-armv7.md](proposed/hal-armv7.md)（p002 で書く。この版では上の 3 点の宣言だけ）。**U1（ユーザー）: この 3 点を「同じ形で事前承認」にするか。**

- **新しい syscall `cacheflush(addr, len)`**（§2.3 の `__clear_cache`: ARMv7 の user は icache を自分で操作できない）: これは kernel の syscall の表（`src/kern/syscall.c`、UAPI）の変更で、HAL の API ではない。JIT（Noct、browser）が要る時に p009 で足す（**Q1 に syscall の番号の割り当てを頼む**）。

### 3.14 BSP の契約（`bsp.h`）

```
void bsp_early_init(void);                 /* MMU on の直後: console、板の識別 */
void bsp_cons_putc(int c); int bsp_cons_getc(void); void bsp_cons_irq_init(void);
void bsp_memory(struct armv7_memory_range *ranges, unsigned *count);   /* RAM と予約 */
const struct armv7_clock_ops *bsp_clock(void);
void bsp_gic_bases(uintptr_t *dist, uintptr_t *cpuif);
int  bsp_cpu_release(unsigned cpu, uintptr_t entry_phys);     /* §3.7 */
void bsp_map_devices(void);               /* identity の section（zybo）か device 窓（virt） */
const void *bsp_kernel_handoff(void);    /* kern_boot_handoff（memory map、boot device、command line） */
int  bsp_l2_present(void);               /* zybo の §1.4 */
```

`locore-board.h`（arm64 と同じ）: 板ごとの `armv7_board_core_index`（MPIDR → 番号）と image の置き直し（zybo の uImage は link の場所に来るので不要、QEMU の ELF も同じ）。

## 4. 起動の道と BOOT.BIN

### 4.1 選ぶ物: **U-Boot の SPL + U-Boot（`xilinx_zynq_virt_defconfig`、`DEVICE_TREE=zynq-zybo`）**

| 案 | 中身 | license | 判断 |
| --- | --- | --- | --- |
| A（採る） | U-Boot v2025.07 を host で build: `spl/boot.bin`（SPL + `ps7_init_gpl.c` の ZYBO の設定、`mkimage -T zynqimage`）と `u-boot.img`。SD の FAT に `BOOT.BIN`・`u-boot.img`・`boot.scr`・`uvmunix`・`zynq-zybo.dtb`。Vivado も embeddedsw も要らない | GPL-2.0+（`Licenses/README`、`spl.c`・`ps7_init_gpl.c` の SPDX）。binary は tree に入れず **zedBSD-firmware の `zybo-uboot/`** に置き、source の tag・config・build の手順・`Licenses/gpl-2.0.txt` を添える（GPL の配布の条件: source の所在を書く） | SPL は FSBL と同じ BootROM の header（`mkimage -T zynqimage`）。U-Boot の `distro_bootcmd` が FAT の `boot.scr` を実行する（`zynq-common.h` の `BOOT_TARGET_DEVICES_MMC`、`board.c` の `modeboot=sdboot`） |
| B | embeddedsw の FSBL（MIT）+ Vivado が生成する `ps7_init.c`（Vivado の export、license は Vivado の物 = **未確認**）+ U-Boot | FSBL は MIT だが ps7_init の生成物の license と Vivado の導入が要る（ユーザーが FPGA の toolchain を入れる予定だが、kernel の移植がそれを待つ理由は無い） | 後で PL の bitstream を BOOT.BIN に入れて起動時に PL を書きたくなった時の案。今は採らない |
| C | FSBL が kernel の ELF を直に読む（`image_mover.c`）、U-Boot 無し | B と同じ + kernel の更新に bootgen が要る | SSH で kernel を替える運用に合わないので採らない |

- `boot.scr`（`platform/armv7/zybo/boot.cmd` → `platform/riscv64/tools/make-uboot-script.py` を流用、GPL の mkimage は要らない）: `setenv bootargs "<cmdline.txt の中身>"`、`load mmc 0:1 ${kernel_addr_r} uvmunix`、`load mmc 0:1 ${fdt_addr_r} zynq-zybo.dtb`、`bootm ${kernel_addr_r} - ${fdt_addr_r}`。`bootm` は uImage を load address `0x00100000` に写し、r0=0・r1=machine id・r2=DTB で入口へ（kernel は r2 の DTB の magic `0xd00dfeed` で有無を見る）。DTB の `local-mac-address` は U-Boot の fdt fixup が `ethaddr` から書く（U-Boot の env に `ethaddr` が無い時は書かれない → §5.4 の MAC の既定）。
- DTB は Linux の `zynq-zybo.dts`（GPL-2.0）を U-Boot の build が出す `arch/arm/dts/zynq-zybo.dtb`（GPL-2.0+）で済ませ、同じ firmware の directory に置く（VF2 の `dtb.LICENSE` と同じ扱い）。kernel は DTB 無しでも固定の address で動く（QEMU の ELF boot）。
- SD の image（`platform/armv7/disk-image.mk`）: MBR、partition 1 = FAT32（`BOOT.BIN`、`u-boot.img`、`boot.scr`、`uvmunix`、`zynq-zybo.dtb`、`cmdline.txt`、`LICENSE` の群）、partition 2 = UFS root（rpi4 の `check-rpi4-hdd-image.py` と同じ並び。root は `overlay-root=boot0:rootfs.img` の既定の形か、VF2 の native の UFS partition か: **rpi4 と同じ「FAT の rootfs.img の overlay」を採る**（kernel の既定の `KERN_IMAGE_BOOT_PARAMETERS_TEXT` のまま動く））。firmware の取得は `platform/armv7/zybo/firmware.mk`（`platform/arm64/firmware.mk` の形、`zedbsd-firmware.mk` の mirror → upstream は無い（自分で build した物）ので **mirror だけ**、size と SHA-256 を manifest に）。
- **U-Boot の build は P1 でなく Q1/ユーザーの host の作業**（toolchain の規則には当たらないが、tree の外の GPL の build。手順書を `plan/ws286/firmware/uboot-build.md` に書く: `make xilinx_zynq_virt_defconfig DEVICE_TREE=zynq-zybo CROSS_COMPILE=arm-linux-gnueabihf-`、Debian の `gcc-arm-linux-gnueabihf` と `u-boot-tools`。host の `build/llvm` は使わない）。

### 4.2 kernel の更新と再起動（運用）

- 板で: `/boot/boot0` に FAT を mount（VF2 と同じ）、`uvmunix` を置き換え、`sync`。`plan/tools/zybo/deploy.sh VMUNIX`（`plan/tools/vf2/run.sh` の写し: scp、`cmp` で読み戻し、SwitchBot の cycle（plug は既存の物、ID はユーザー）、SSH の復帰を待つ）。Q1 が実行する（switchbot.py は Q1 だけ）。
- IP は serial で確かめる（ユーザー）: host の `/dev/ttyUSB1`（FT2232H の B channel、まだ未接続）に 115200。`plan/tools/guest/serial.py` は socket だけなので **`--device /dev/ttyUSBn` を足す変更を Q1 に頼む**（tool は Q1 の物）。login の prompt の確認は、QEMU は `boot-test.sh` の PNG でなく **serial の文字列**になる（framebuffer が無い。§8.1 の `BOOT_MODE=zybo-serial` の案）。
- JTAG（FT2232H の A channel、OpenOCD）: 後の option。U-Boot を JTAG で流す/ kernel を `load_image` する道。今回は設計に含めない（U1 の後）。

### 4.3 QEMU の起動

- `xilinx-zynq-a9`: `qemu-system-arm -M xilinx-zynq-a9 -smp 2 -m 512M -kernel build/zybo/vmunix -serial unix:… -drive file=sd.img,if=sd,format=raw -nic user,model=cadence_gem -display none`。UART1 は 2 つ目の serial（QEMU は UART0 を最初の `-serial` に当てる: `xilinx_zynq.c` 298〜306 行: `0xE0000000` が serial 0、`0xE0001000` が serial 1）→ **`-serial null -serial unix:…`**（UART1 を 2 つ目に）。
- `virt`: `qemu-system-arm -M virt -cpu cortex-a15 -smp 2 -m 512M -kernel build/armv7-virt/vmunix -serial unix:… -display none`（virtio の block/net は後）。

## 5. BSP と driver（zybo）

### 5.1 UART（Cadence `cdns,uart-r1p8`）

- HAL の early console（`bsp-zybo/uart.c`）: U-Boot が設定した baud を保ち（CR の TX/RX enable だけ確かめる）、`FIFO`（+0x30）に書く前に `SR`（+0x2C）の TXFULL（bit 4）を待つ。
- kernel の driver `src/drivers/serial/cdns-uart.c`（`ns16550.c` の形: `drv_cdns_uart_probe(fdt|fixed, port)`、`drv_cdns_uart_start_input(port, receive)`）: RX の割り込み（`IER` の RXTRIG（bit 0）と TOUT（bit 8）、`RXWM` = 1、`RXTOUT` = 10）で 1 byte ずつ `drv_serial_console_receive` へ。`ISR` に書いて clear。platform の binding `src/kern/platform/zybo.c` は `riscv-virt.c` の形（`drv_serial_console_init` + `probe_uart`）。
- Values referenced: Linux v6.12 `drivers/tty/serial/xilinx_uartps.c`（`CDNS_UART_*` の offset と bit）。

### 5.2 SLCR（clock・reset・MIO）

`bsp-zybo/slcr.c` + kernel の `src/drivers/platform/zybo/zynq-slcr.c`: unlock/lock（`0x008`/`0x004`、0xDF0D/0x767B）、`APER_CLK_CTRL` の bit の on/off、`GEM0_CLK_CTRL` の divisor の書き換え（速度の変更）、`FPGA_RST_CTRL`・`LVL_SHFTR_EN`（PL）、`PSS_RST_CTRL`（reset）、`A9_CPU_RST_CTRL`（CPU1）。MIO の pinmux は **変えない**（U-Boot の `ps7_init_gpl` が ZYBO の配線を済ませる。kernel は読んで確かめるだけ: UART1 の MIO48/49、SD0 の MIO40〜45、GEM0 の MIO16〜27、USB0 の MIO28〜39）。QEMU の zynq でも同じ register が有る（`zynq_slcr.c`）。

### 5.3 SD（Arasan SDHCI 8.9a）

- `src/drivers/platform/rpi4/rpi4-sdhci.c`（PIO、CMD18/25 の run、BUG-295 の回復）を **`src/drivers/mmc/sdhci.c` の共通の core**（`drv_sdhci_attach(config)`: registers、base clock Hz、quirk、割り込み番号 or poll）に切り出し、rpi4 の file は glue に、zybo の glue `zynq-sdhci.c` は base clock を SLCR の SDIO_CLK_CTRL から（50 MHz）、`SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN`（capability の base clock 0）、`CLOCK_DIV_ZERO_BROKEN`（divisor 0 を使わない）、`PRESET_VALUE_BROKEN`（Linux `sdhci-of-arasan.c` の `sdhci_arasan_pdata`）。
  - **rpi4 の file を触るのは Pi 4 の回帰が要る**。代案: zybo 用に rpi4-sdhci.c を写して `zynq-sdhci.c` にし、共通化は後（riscv32 の HAL と同じ判断）。**採るのは代案（写し）**: WS278（Pi 5）が rpi4/rpi5 の file を動かしている最中で衝突するため。共通化は Future Work。
  - 最初は PIO・poll（rpi4 と同じ）。QEMU の `sdhci.c` の model で CMD/DAT の流れを確かめてから実機。DMA（SDMA、32 bit）は速さが要る時の後の Phase。
- card detect: SD0 の CD は MIO47（SLCR `SD0_WP_CD_SEL`）。U-Boot の後なので無視して「刺さっている」前提で初期化し、失敗なら boot device 無し。

### 5.4 LAN（Cadence GEM0 + RTL8211E）

- **`src/drivers/ethernet/cadence-gem.c` を新しい共通の core として書く**（`drv_cadence_gem_attach(const struct drv_cadence_gem_config *)`: registers、irq、descriptor の幅 32/64、DMA の制約、`set_tx_clock(speed)` の callback、MDIO の clock divisor、MAC、PHY の ops（`drv_mii_bus` + 板の PHY の初期化 callback）。中身は `rp1-ethernet.c` の GEM の部分（ring、`reclaim_transmitted`、`receive_packets`、`gem_poll`、MDIO）を 32 bit の descriptor で書き直す。**`rp1-ethernet.c` は変えない**（WS278 が動かしている。RP1 を core に乗せ替えるのは後の WS、Future Work）。dw-eqos.h と同じ config の形。
- zybo の glue `src/drivers/platform/zybo/zynq-ethernet.c`: `APER_CLK_CTRL` の GEM0、`GEM0_CLK_CTRL` の divisor を link の速さで 125/25/2.5 MHz に（Linux `macb_set_tx_clk` の rate、divisor は §1.2）、`GEM0_RCLK_CTRL`（`0xF8000138`）の enable、RGMII（GEM の `NET_CFG` の gigabit・`USRIO`? Zynq は GEM の `net_cfg` で RGMII を選ぶのでなく SLCR `GEM0_CLK_CTRL` と MIO の設定で決まる: p008 で確かめる）、MDC = pclk 108 MHz / 64 ≈ 1.7 MHz（`NET_CFG` の MDC_CLK_DIV = 0b100（div 64）; macb.h の `MACB_CLK_DIV64`）、RXUBR の割り込みで receiver を再起動（Linux `MACB_CAPS_NEEDS_RSTONUBR`: zynq の GEM の癖）。
- PHY: `src/drivers/ethernet/rtl8211e.c`（`bcm54213pe.c` の形: `drv_rtl8211e_initialize(phy, &identity)`、`read_link`）: ID `0x001cc915`、RGMII-ID の delay は extension page 0xa4 の register 0x1c の bit 13|12|11（Values referenced: Linux `realtek.c` `rtl8211e_config_init`: page select 0x1f = 7、ext page 0x1e = 0xa4、0x1c を書き、page を戻す）、autoneg は `drv_mii_phy_start_autonegotiation(bus, 0, gigabit=1)`、link は `drv_mii_phy_read_link`。ZYBO の RTL8211E の TXDLY/RXDLY の pin の strap は Digilent の回路図で未確認 → delay は register で明示する（Linux と同じ）。
- MAC: U-Boot が DTB に書いた `local-mac-address` → boot parameter `mac0=`（新しい parameter、`kernel-boot-parameters.md` に足す）→ どちらも無ければ locally administered の乱数（警告）。QEMU は `-nic user,model=cadence_gem,mac=…` を DTB 無しで渡せないので乱数で動く。

### 5.5 USB0（ChipIdea host、ULPI の USB3320）

- 事実: ChipIdea は EHCI 互換（caps = base + 0x100、ops = caps + CAPLENGTH（0x40）= base + 0x140。`USBMODE` = ops + 0x68（`ci_regs_nolpm`、zynq は lpm 無し）で CM = host（0b11）、`PORTSC` の PTS[31:30] = 0b10（ULPI）、`ULPI_VIEWPORT` = ops + 0x30）。
- 手順（p010）: (1) MIO46 の GPIO（PS GPIO `0xE000A000`、bank 1 の bit 14: DATA/DIRM/OEN）で PHY を reset（low → 10 ms → high）。(2) `APER_CLK_CTRL` の USB0。(3) `USBCMD.RST`、`USBMODE` = host|SLOM、`PORTSC.PTS` = ULPI。(4) ULPI viewport で PHY の vendor ID（0x0424 = Microchip/SMSC）を読み、OTG control に `DrvVbus|DrvVbusExternal`、`DP/DM pulldown`、function control を書く（Procedure referenced: U-Boot v2025.07 `drivers/usb/host/ehci-zynq.c` `ehci_usb_probe` と `drivers/usb/ulpi/ulpi.c`）。**ZYBO の host 口の VBUS の供給（外部電源が要るか、USB3320 の DrvVbus の配線）は Digilent の reference manual で未確認** → p010 の実機の最初の確認項目。(5) `src/drivers/pci/pci-ehci.c` の EHCI の core を `src/drivers/usb/ehci.c` に切り出し（PCI の 17 か所の呼び出しは attach/detach に局在: `drv_pci_device_map_bar/claim_bar/establish_irq/dma/set_bus_master/save・restore_enable_state`）、`drv_ehci_attach(config)`（registers、irq、dma device、ChipIdea の有無）と `pci-ehci.c` の glue、zybo の glue `zynq-usb.c`。**これは pci-ehci.c の refactor = amd64 の USB の回帰が要る**（T1 の amd64 の USB の HID/storage の試験で確かめる。切り出しは関数の移動だけ、動作は変えない）。
- 制限: 今の EHCI の driver は **high-speed の device だけ**（`pci-ehci.c` 642 行: FS/LS の port は companion に渡す。split transaction は無い）。ChipIdea は companion を持たず内蔵の TT で FS/LS を扱うので、**keyboard・mouse（LS）と FS の device は WS286 では動かない**（HS の storage・WLAN の dongle・HS hub は動く。hub の下の LS/FS も split が要るので動かない）。split transaction の実装は別 WS（Future Work、amd64 にも利く）。**U2: USB の範囲をこれで良いか。**

### 5.6 PL の書き込み（devcfg/PCAP）と `fpgaload`

- kernel: `src/drivers/platform/zybo/zynq-devcfg.c`、cdev `/dev/fpga0`（0600 root。`src/kern/devfs.c` の表に足す = **Q1 に頼む path**）。`write(2)` で bitstream を kernel の buffer（最大 4 MiB、7Z010 の全体の bitstream は約 2.08 MB。`drv_dma_alloc_coherent`（uncached、32 bit）に順に積む）に貯め、`ioctl(FPGA_PROGRAM)` で書く（`close` で書く案は、途中で落ちた時の区別が付かないので採らない）。`ioctl(FPGA_STATUS)` で `PCFG_DONE`・`INIT`・最後の error を返す。sysctl `hw.fpga.done`。
- 手順（Procedure referenced: Linux v6.12 `drivers/fpga/zynq-fpga.c` `zynq_fpga_ops_write_init`・`zynq_step_dma`・`zynq_fpga_ops_write_complete`、Values referenced: 同 file の `*_OFFSET`・`*_MASK`）:
  1. SLCR unlock、`PCAP_CLK_CTRL` enable。devcfg `UNLOCK`（+0x34）= `0x757BDF0D`（U-Boot の `cpu.c` も書く）。
  2. sync word（`0xAA995566`、little endian の word の列で `66 55 99 AA`）が無ければ `EINVAL`（Vivado の `.bit` は userland が header を外し、word ごとに byte を反転して `.bin` の並びにする。kernel は `.bin` の並びだけ受ける）。
  3. `FPGA_RST_CTRL`（SLCR +0x240）= 0xF、`LVL_SHFTR_EN`（+0x900）= 0 → 0xA（PS→PL）。
  4. `CTRL`（devcfg +0x00）の `PCFG_PROG_B`（bit 30）を 1 → `STATUS`（+0x14）の `PCFG_INIT`（bit 4）を待つ → 0 → `!INIT` を待つ → 1 → `INIT` を待つ（各 2.5 s の timeout、20 µs ごと）。
  5. `CTRL` |= `PCAP_PR`（bit 27）| `PCAP_MODE`（bit 26）。`STATUS` の DMA queue が空（`DMA_Q_E` bit 30 = 1、`DMA_Q_F` bit 31 = 0）でなければ `EBUSY`。`MCTRL`（+0x80）の `PCAP_LPBK`（bit 4）を落とす。
  6. DMA: `DMA_SRC_ADDR`（+0x18）= buffer の物理 | 1（最後の転送の印）、`DMA_DST_ADDR`（+0x1C）= `0xFFFFFFFF`、`DMA_SRC_LEN`（+0x20）= word 数、`DMA_DEST_LEN`（+0x24）= 0。buffer が複数なら最後だけ | 1。
  7. `INT_STS`（+0x0C）の `D_P_DONE`（bit 12）を割り込み（devcfg IRQ 40）か poll で待つ（5 s）。error の bit（`0x00F0C860`）なら失敗。
  8. `PCFG_DONE`（bit 2）を待つ、`CTRL` の `PCAP_PR` を落とす、`LVL_SHFTR_EN` = 0xF（PL→PS も）、`FPGA_RST_CTRL` = 0。
- userland `userland/base/fpgaload/`（`fpgaload FILE.bit|FILE.bin`、`fpgaload --status`）: `.bit` の header（0x0009 の magic、a〜e の field、`e` の長さの後が data）を読んで外し、word の byte を反転、sync word を確かめて `/dev/fpga0` に write → `ioctl`。SSH で `scp design.bit zybo:` → `fpgaload design.bit`。PL の clock FCLK0 は U-Boot の設定（100 MHz）のまま。
- QEMU の zynq は devcfg の DMA の model（`xlnx-zynq-devcfg`）を持つので、手順の流れ（register の遷移）は QEMU で確かめられる（PL は無いので `PCFG_DONE` は model が立てる）。

### 5.7 reset・watchdog

`hal_reset` は §3.13（SLCR）。SWDT（`cdns,wdt-r1p2`、`0xF8005000`）は今回は使わない。再起動は SwitchBot。後の option: 「hang したら 60 s で自動 reset」の SWDT（`ZMR` の WDEN|RSTEN、`CCR` の counter、`RESTART` = 0x1999 を tick で蹴る）を `reboot=watchdog` の boot parameter で。Values referenced: Linux `cadence_wdt.c`。

### 5.8 QSPI（後）

`xlnx,zynq-qspi-1.0`（`0xE000D000`）、linear の読み（`0xFC000000`）で boot の flash を読むだけの driver は WS286 の外（Future Work）。

### 5.9 platform の binding（`src/kern/platform/zybo.c`、`armv7-virt.c`）

`riscv-virt.c`・`visionfive2.c` の形: `kern_platform_init` は console（serial）、SD の disk を boot device に、`kern_platform_refresh_devices` で GEM・USB（割り込みが有効になってから）、`kern_platform_reboot` → `hal_reset`、poweroff は unsupported。virt は virtio-mmio の block/net を後の Phase（riscv-virt の virtio の driver が有れば流用: `src/drivers/platform/riscv-virt/` を読む）。

## 6. 資源・並行性・失敗

- 所有: page table は space（HAL が割り当て、`hal_space_destroy` で L2 の table と L1 を pmem に返す。kernel の L1 は固定）。uncached の alias は呼び手。descriptor ring は driver（platform が kernel の寿命で持つ: rp1-ethernet の方針と同じ）。devcfg の buffer は `/dev/fpga0` の open file description（最後の close で解放）。
- lock: GIC の distributor は spin（§3.4）、ASID の割り当ては spin、L2 の PL310 の操作は per-CPU で呼べる（register は atomic。`CACHE_SYNC` は複数 CPU から同時に書いて良い）、devcfg は 1 つの mutex（同時の program は `EBUSY`）。driver の割り込みは boot CPU（ws208 の方針）。
- 割り込みと DMA: 全部 non-coherent（§3.12）。driver は `drv_dma_sync_for_cpu/device` を使う（dw-eqos・rpi4 と同じ）。L2 が有る時は `hal_dcache_*` が outer も扱うので driver は意識しない。
- 失敗と回復: CPU1 が来ない → 1 CPU で続ける（§3.7）。L2 の ID が読めない → L2 無し。SD の初期化の失敗 → boot device 無しで `VFS initialization failed` の段で止まる（riscv32 と同じ到達点）。GEM の RXUBR → receiver の再起動。PHY が無い（ID 0）→ link down のまま。PCAP の timeout → `ETIMEDOUT`、`FPGA_RST_CTRL`・`LVL_SHFTR_EN` を元に戻さない（PL は未定義）、`--status` で見える。panic は UART に register dump（`debug.c`）、`hal_cpu_panic_all` は SGI 1。

## 7. 試験の計画

### 7.1 host（build と短い試験、担当が流す）

- `make ZEDBSD_PLATFORM=zybo vmunix`・`armv7-virt`（warning 0）。`llvm-nm -u` で `__atomic_*`・未解決の `__aeabi_*` が無い。`readelf -S` で `.ARM.exidx` が無く、text が RX の section に収まる。
- host の unit test（`plan/ws286/tests/`、`plan/tools/ports/mips-pte-host-test.c` の形）: `pte-host-test.c`（descriptor の組み立て: 属性・AP・AF・nG・ASID）、`uimage-host-test.py`（header の CRC）、`bitstream-host-test.c`（`.bit` の header の解析と byte の反転、sync word）。

### 7.2 QEMU（T1 が流す。担当は QEMU を起動しない）

- `plan/tools/boot-test.sh` に **`BOOT_MODE=armv7-virt`**（`qemu-system-arm -M virt -cpu cortex-a15 -smp 2 -m 512M -kernel …`）と **`BOOT_MODE=zybo`**（`-M xilinx-zynq-a9 -smp 2 -m 512M -serial null -serial unix:… -drive if=sd …`）を **Q1 が足す**（案は [proposed/boot-test-armv7.md](proposed/boot-test-armv7.md)、p002 で書く）。判定は PNG でなく **serial の文字列**（framebuffer 無し）: `boot-test.sh` の riscv64-virt の serial の socket を `plan/tools/guest/serial.py expect` で読む形。AGENTS.md の「console log を読んで判定しない」は、serial で対話する `serial.py` の `expect` の使い方に沿う（login の prompt を serial で待つ）。
- 到達点の文字列（riscv32 と同じ形）: p002 `ARMV7 HAL READY`、`PAGING PASS`（§7.4 の self-test が全部通った時だけ）、`TIMER TICK`; p003 `ARMV7 SMP 2 ONLINE`; p005 `VFS initialization failed`（SD 無し）/ `login:`（SD 有り）。
- 解析は gdbstub（`-S -gdb tcp::port`、host の `gdb-multiarch`、`set architecture armv7`）。

### 7.3 実機（T1 の依頼で、Q1 が deploy.sh と SwitchBot を回す）

- p007: SD の image を書いた card で U-Boot → `uvmunix` → UART1 に `ARMV7 HAL READY … TIMER TICK`、`SMP 2 ONLINE`、SD の root を mount、`login:`。
- p008: `ifconfig`/DHCP、host から ping、SSH の login。`scp` で `uvmunix` を替えて SwitchBot で再起動 → 新しい kernel の `uname`。
- p010: USB の HS の storage を挿して `/dev/sd*` が出る。
- p011: `fpgaload` で Digilent の ZYBO の sample の `.bit`（LED の点滅など、ユーザーが Vivado で作る）→ LED が変わる、`--status` が done。
- 実機の証拠は UART の log の抜粋と写真（ユーザー）で、QEMU の証拠と分けて書く。

### 7.4 self-test（`PAGING PASS` の条件、mips の §6.1 に倣う）

kernel mode から user の address（TTBR0）を使って: (1) 4 KiB の map と読み書き、(2) AF=0 の page の最初の読みで access flag fault が HAL の中で解決されて `ACCESSED` が立つ、(3) RO で map した writable の page への store で permission fault → `DIRTY` が立って書ける、(4) `hal_space_prot_query` で write を外すと `DIRTY` が返り、次の store は fault、(5) ASID を変えて同じ仮想が別の物理を指す、(6) `TLBIMVAIS` の後に CPU1 からも新しい mapping が見える（p003 以降）、(7) direct map と uncached の alias で device 窓の書き込みが RAM に届く（L2 有りの実機では clean の確認）。落ちたら `PAGING FAIL <番号>` で止まる。

## 8. Phase と LW（8 LW ≈ 2〜3 時間）

| Phase | 目的と到達点 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 | 6 | — |
| p002 | **toolchain の確認と HAL の入口（virt）**: `include/hal/arch/armv7.h`・`arch.h`、`src/hal/armv7/` の locore・vectors・page（boot の L1）・lib・cmain、bsp-virt（PL011、FDT、GICv2 の base）、`platform/armv7/`（toolchain.mk・vmunix.mk・vmunix.ld・Makefile）、`Makefile`・`menuconfig.py` の `armv7-virt`・`zybo`、`config/ci/config-armv7-virt.mk`・`config-zybo.mk`、`int64.c` の `__aeabi_*`、host の pte test。到達点: virt の serial に `ARMV7 HAL READY`・`PAGING PASS`・`TIMER TICK`（generic timer）（T1） | 14 | Q1: LLVM に `ARM`（§2.1 案 1）、boot-test の mode。U1 |
| p003 | **例外・GIC・task・SMP（virt）**: int.c の分類、irq.c/gic.c、clock（generic timer の tick）、task.c の switch/fork/exec/signal、PSCI の CPU_ON、IPI。到達点: `ARMV7 SMP 2 ONLINE`、`VFS initialization failed`（T1） | 14 | p002 |
| p004 | **space・ASID・cache・VFP（virt）**: space.c（ASID、TLBI の broadcast、§7.4 の 1〜6）、cache.c（L1）、fpu.c の lazy、atomic の確認。到達点: self-test 全通 + init（userland 無しで `VFS initialization failed` まで）（T1） | 12 | p003 |
| p005 | **zybo の BSP（QEMU xilinx-zynq-a9）**: bsp-zybo（Cadence UART、固定の memory、SLCR、private/global timer、CPU1 の park と release、§1.4 の判定）、`src/kern/platform/zybo.c`、cdns-uart.c の driver、zynq-sdhci.c（PIO）、SD の image（disk-image.mk、uimage の tool、boot.cmd）。到達点: QEMU zynq で `SMP 2 ONLINE`、SD の root から `login:`（serial）（T1） | 14 | p004 |
| p006 | **実機の起動**: U-Boot の BOOT.BIN（Q1/ユーザーが build、firmware の mirror）、firmware.mk、deploy.sh、L2（PL310）の初期化と outer の cache 操作、CPU1 の OCM の起こし方の確認、実機の `login:`（UART） | 10 | p005、BOOT.BIN（ユーザー/Q1）、SD card・UART の接続（ユーザー） |
| p007 | **LAN**: cadence-gem.c（core）、zynq-ethernet.c（glue、tx clock）、rtl8211e.c、MAC の受け渡し。QEMU zynq の GEM の model で `-nic user` の DHCP → 実機で SSH の login、`uvmunix` の SSH 更新 + SwitchBot の再起動 | 14 | p006 |
| p008 | **userland と sysroot**: §2.1 案 2（Q1）、sysroot.mk の armv7（Q1）、crt0/crt1/setjmp、elf.c、stat.h、`cacheflush` の syscall（Q1 の番号）、rootfs の build（`NO_DESKTOP_ARCHITECTURES` に armv7）、sshd・networkd が動く（p007 の SSH はこれに依る: **p007 と p008 は入れ替えて p008 を先に**。表の順は依存で読む） | 16 | p005、Q1 の toolchain | 
| p009 | **USB0**: EHCI の core の切り出し（pci-ehci.c の refactor、amd64 の USB の回帰を T1）、zynq-usb.c（GPIO の PHY reset、USBMODE、ULPI）、HS の storage。実機の VBUS の確認 | 14 | p006、T1 の amd64 の回帰 |
| p010 | **PL の書き込み**: zynq-devcfg.c、`/dev/fpga0`、`fpgaload`、QEMU の devcfg の model で手順、実機で LED の bitstream | 10 | p006、ユーザーの `.bit` |
| p011 | 規約の全文の見直し（conformance） | 5 | p002〜p010 |

合計 139 LW（約 35〜52 時間）。p002〜p004（virt）と p005（zynq の QEMU）は T1 の QEMU だけで進み、実機は p006 から。p008（userland）は p005 の後に p006 と並行できる（toolchain は Q1）。

## 9. risk と敵対的な読み直し

| # | risk | 対処 |
| --- | --- | --- |
| R1 | LLVM の ARM の target が共有の toolchain に無く、Q1 の作り直し（patch level 11）を待つ | p002 の最初に `--print-targets`。待つ間は host の test と文書 |
| R2 | `__aeabi_*` の wrapper（asm）と 64 bit の atomic の inline が揃わず link が落ちる | p002 で `llvm-nm -u` を受け入れ条件に。落ちたら ENTER style と int64.c の asm |
| R3 | CPU1 の BootROM の待ち方（`0xFFFFFFF0`）が UG585 未読の推測 | fallback（SLCR の reset + trampoline、Linux の方法）を同じ Phase に実装。両方駄目なら 1 CPU |
| R4 | QEMU の zynq の timer が SLCR を見ない（100 MHz 固定）ので実機と周波数が違う | §1.4 の L2 の ID で判別。判別が効かない時は boot parameter `periphclk=` を足す |
| R5 | A9 + PL310 の outer cache の clean を忘れると DMA の descriptor が壊れる（実機だけで出る） | p006 の最後まで L2 off で走り、L2 on は単独の step。uncached の alias は L2 を通らない。§7.4 の (7) |
| R6 | access flag と dirty の software 追跡（§3.3）で fault が増え、user の性能が落ちる | 650 MHz の板なので許容。AF fault は 1 page 1 回 |
| R7 | USB の LS/FS が動かない（split 無し） | U2 で範囲を決める。HS の storage と hub で受け入れ |
| R8 | pci-ehci.c の refactor が amd64 の USB を壊す | 関数の移動だけ、T1 の amd64 の USB の回帰を p009 の受け入れに |
| R9 | ZYBO の USB host の VBUS が外部電源や別の pin に依る（未確認） | p009 の最初に実機で PORTSC の CCS と VBUS を見る。駄目なら Digilent の manual を読んで GPIO を足す |
| R10 | U-Boot の bootm が uImage を `0x00100000` に写す時に DTB（`fdt_addr_r` = 0x1f00000 付近）や自身と重なる | image は 2〜3 MiB、`kernel_addr_r`（0x2000000）→ 0x100000 の copy。cmain が DTB を最初に写す |
| R11 | Zynq の GEM は RGMII の設定が SLCR と MIO に散っていて、QEMU の model では見えない | QEMU で ring と割り込みだけ確かめ、PHY と clock は実機。U-Boot の `ethaddr` と `ps7_init` の MIO の値を p007 で読む |
| R12 | 2 つの BSP（virt・zybo）で timer と intc の抽象が増え、HAL が太る | `armv7_clock_ops` と GIC の base だけに絞る（§3.6・§3.14）。virt は PL011 と PSCI の小さな file |
| R13 | serial の判定（PNG でなく文字列）が今の boot-test.sh の形と違う | Q1 の mode の追加で `serial.py expect` を使う。AGENTS.md の規則（log を読んで判定しない）は、serial で対話する既存の道具の範囲で守る |

敵対的に読み直した点: (1) **TTBR0/TTBR1 の split N=1 で user の L1 が 8 KiB**: space ごとの L1 を 8 KiB 揃えで割る。`hal_space_create` の失敗は pmem の不足だけ。(2) **kernel からの user の address の access**（`uaccess`）は TTBR0 経由で普通に通る（domain は 1 つ、DACR = client）。PXN は無い（A9 は短い descriptor に PXN を持たない）ので kernel が user の page を実行し得る: `SCTLR` では防げない、`hal_task_exec_validate` と kernel の code の規律で守る（既存の 32 bit port と同じ）。(3) **`srsdb`/`cps`/`rfeia`** は ARMv6 以降の命令で A9・A15 に有る。(4) **global timer の 64 bit の読み**は上→下→上で揃える。(5) **spsr.T** を持つ frame を user が壊せない（kernel が書く）。(6) **VBAR** は secure の物（NS 切り替え無し）。(7) **`hal_pmem_map_uncached` の前の clean+invalidate は L2 も**（§3.11）。(8) **QEMU virt の `-cpu cortex-a15` は LPAE を持つが TTBCR.EAE=0 で短い descriptor**: 問題無し。(9) **DTB の有無で分岐する cmain** は zybo の QEMU（無し）と実機（有り）の両方で通す。(10) **syscall の r7** は AAPCS の callee-saved なので libc の wrapper が保存して使う（`__syscall6`）。

## 10. ユーザー・Q1 の判断（提示）

| # | 問い | 勧め |
| --- | --- | --- |
| U1 | HAL の API の差分（§3.13 の 3 点）を WS263〜271 と同じ形で事前承認するか | 同じ形で事前承認 |
| U2 | USB の範囲: HS の device だけ（LS/FS は split transaction の別 WS） | HS だけで受け入れ、split は Future Work |
| U3 | BOOT.BIN は U-Boot（SPL + U-Boot、GPL-2.0+、zedBSD-firmware に binary と source の所在）で良いか | 良い（Vivado・embeddedsw 不要）。build は Q1 かユーザーの host |
| U4 | SD の root は rpi4 と同じ「FAT の rootfs.img の overlay」で良いか | 良い（既定の boot parameter で動く） |
| U5 | QEMU の zynq の判定（serial の文字列）と boot-test.sh の mode の追加 | Q1 が `armv7-virt`・`zybo` の mode を足す（serial の expect） |
| U6 | user の code は ARM mode だけ（Thumb-2 は Future Work） | ARM だけ |

Q1 への依頼: (1) toolchain: §2.1 案 1（p002 の前）、案 2 と sysroot（p008 の前）。(2) boot-test.sh の 2 mode。(3) `serial.py --device`。(4) `src/kern/devfs.c` の `fpga` の行、`cacheflush` の syscall の番号。(5) Guardrail・Master の Tools の登録、Queue と Master の写し。(6) U-Boot の build（手順書は p006 で書く）。

## 11. 選ばなかった案と理由

- **LPAE（長い descriptor）**: A9 に無い。短い descriptor 一本。
- **Thumb-2 の kernel/user**: 速さは要らず、例外の入口と signal の handler の扱いが増える。
- **FSBL（MIT）+ Vivado の ps7_init**: Vivado の導入と生成物の license の確認が要り、kernel の更新の運用（SSH で `uvmunix` を替える）に U-Boot が要るので U-Boot 一本。
- **ELF を U-Boot の `bootelf` で**: `xilinx_zynq_virt_defconfig` に `CMD_ELF` が有るか未確認で、DTB の受け渡し（`bootm` の r2）が無い。uImage + `bootm`。
- **GEM の core を rp1-ethernet.c から切り出す**: WS278 が同じ file を動かしている最中で衝突するので、新しい core を zybo から使い、RP1 の乗せ替えは後。
- **SDHCI の共通化を今やる**: 同じ理由（rpi4 の回帰）。写しで始める。
- **IPI の TLB shootdown**: A9/A15 の broadcast で足りる。
- **CPU1 を SLCR の reset だけで起こす**: QEMU で効かない（§1.3）ので、park と OCM を主にし reset は fallback。
- **PNG の boot test**: framebuffer が無い。serial の expect。

## 12. 未反映の決定（2026-10-23、Q1 経由。引き継ぎの担当が §4・§8・§10 を書き直す）

- ユーザー:「SwitchbotはまだZYBOに接続されていません。USB給電になっています。JTAGからリセットできるならそっちの方がいいですね。ひとまず、openFPGALoaderやopenxc7など、OSSのみで実装を進めてください。難しいとなったらAMDのツールを導入します。」「JTAGが簡単に利用できるなら、SDブートよりも先にJTAGブートを実装してください。」
- 事実（Q1）: host に openocd 0.12.0・openFPGALoader 0.13.1。`sudo openFPGALoader -c digilent --detect` で chain: index 0 Cortex-A9 DAP（0x4ba00477）、index 1 xc7z010（0x3722093）。`/dev/ttyUSB0` = JTAG（FT2232H A）、`/dev/ttyUSB1` = UART（B）。ZYBO は USB 給電、boot mode の jumper は JTAG。
- 反映すべき事: (1) 道具は OSS だけ（U-Boot SPL の `ps7_init_gpl` で DDR/clock/MIO、bitstream は openXC7、書き込みは openFPGALoader か devcfg）。(2) 開発は JTAG が主: OpenOCD（`interface/ftdi/digilent_*` + `target/zynq_7000.cfg`）で reset → SPL（`spl/u-boot-spl.bin`、OCM `0x00000000`〜 に load。SPL は BootROM の header 無しの raw を `load_image` し、`resume` で `ps7_init` を走らせる。SPL の `boot_device` が JTAG だと U-Boot proper を待つ: `spl.c` の `spl_boot_device` は `ZYNQ_BM_JTAG` で `BOOT_DEVICE_RAM`（未確認、要読み直し））→ kernel の `vmunix.bin` を DDR `0x00100000` に load（dtb は `0x01F00000`、r2 に入れるには OpenOCD の `reg r2` で設定）→ `resume 0x00100000`。reset は SRST の配線が無ければ DAP から SLCR `PSS_RST_CTRL`（`0xF8000200`）= 1（unlock `0xF8000008` = 0xDF0D の後）。UART は `/dev/ttyUSB1` で読む。script は `plan/tools/zybo/jtag-boot.sh`（実装の Phase で作る）。(3) SwitchBot・SD の起動は JTAG の後の Phase（§8 の p006 を「JTAG の起動」に、SD は p006b に）。(4) §3.7 の CPU1: JTAG の起動では BootROM の loop は壊れていない（CPU1 は BootROM の待ちのまま）ので OCM の方法は同じ。OpenOCD の `zynq_7000.cfg` は 2 core を `-coreid 0/1` で持つ: CPU1 を halt したままにしない（`targets zynq.cpu0` だけ使う）。
- 注意: `u-boot-spl.bin` を JTAG で走らせる時、SPL は自身を OCM の `0x00000000` に link している（`CONFIG_SPL_TEXT_BASE`、zynq は 0x0。OCM が低位に有る BootROM 直後の状態が前提。OpenOCD の reset の後は BootROM の JTAG mode が OCM を低位に置く（未確認、p006 の最初に確かめる））。
