<!-- awesome-plan project=zedbsd record=ws286-p001-design -->

# WS286 の設計: Digilent ZYBO（初代、Zynq-7000 XC7Z010）への移植 — ARMv7-A の HAL・toolchain・BSP・driver

2026-10-23 P1（[ws286-p001](phase001/phase.md)、worktree p1-zybo、基点 main 6206d397e）。親: [WS286](ws.md)。
**版 2**（2026-10-23 P1、worktree p1-zybo2、基点 main 151b879de）: §12 のユーザーの決定（OSS だけ、JTAG の起動が先、JTAG で reset、SwitchBot 無し）と、同日の Q1 経由の決定（WS286 の到達目標を「JTAG で起動し PL の block を zedBSD から動かす」に絞る、USB は外す）を反映した。書き直した節: 冒頭、§1.3、§3.7・§3.11〜§3.13、§4（全部）、§5（SD・LAN・USB を外へ、§5.6 の改訂、§5.10 の新設）、§7〜§10、§12。版 1 の SD・LAN・USB・SMP の設計は後の WS の参考として残す（各節の頭に「WS286 の外」と書いた）。
**版 3**（2026-10-23 P2、worktree p2-zybo-d3、基点 main cee5b9421）: 敵対的レビュー [review-20261023.md](phase001/review-20261023.md)（Critical 2・High 5・Medium 11・Low 7）の全部の指摘に対応した。指摘ごとの対応の表は [phase001/phase.md](phase001/phase.md) の「版 3: review の指摘と対応」。書き直した節: §0（版 3 で読んだ物）、§1.3・§1.4、§2.2〜§2.4、§3.2・§3.3・§3.5〜§3.7・§3.10〜§3.14、§4.1・§4.3・§4.6、§5.1・§5.6、§6〜§11（§8 の Phase の表は p006・p007 の範囲を改め、p010 を新設）。各節の版 3 の変更は「版 3:」で始まる文か、置き換えた文そのもの。HAL の API（hal.h）の追加の変更は無い（U1 の 3 点のまま）。

**到達目標（版 2、2026-10-23 Q1 経由のユーザーの決定、§12）**: ZYBO で zedBSD が **JTAG から起動**し（reset も JTAG）、UART の shell から **PL を書き込み**（openFPGALoader の JTAG と、zedBSD の devcfg/PCAP ＋ `fpgaload` の両方）、**PS と PL の間の小さな block**（AXI GP0 の slave の register・LED・switch・button・PL の割り込み、§5.10）を zedBSD から読み書きして動作を確かめる。最短の道は「ARMv7 の HAL（UART・timer・GIC・MMU・task、1 CPU）→ JTAG の起動 → ramdisk の root で shell → devcfg と block」。SMP の CPU1・L2・SD・LAN・SD の起動は PL の動作に要らないので後の WS、USB は WS286 から外す（split の対応は Q1 が WS287 を作る）。
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
| U-Boot（版 2 で追加、GPL-2.0+） | v2025.07 | `arch/arm/mach-zynq/spl.c`（5501d11d146c1e3e）・`cpu.c`（bebe93bfd80716f6）・`u-boot-spl.lds`（97d26214e6c8d73b）・`include/mach/hardware.h`（5c1b7c32e82b54df）、`common/spl/spl.c`（419ff14906623633）・`spl_ram.c`（5d5b7ac28a11bf3a）・`Kconfig`（`SPL_TEXT_BASE`・`SPL_RAM_DEVICE`）、`arch/arm/Kconfig`（`ARCH_ZYNQ` の select）、`configs/xilinx_zynq_virt_defconfig`（502c9bd39d0f5e3a）、`board/xilinx/zynq/Makefile`（0fa8124187af0a78）・`board.c`（6d567b0bc50eb4b9） |
| OpenOCD（GPL-2.0-or-later、host の install を読んだ。値だけ引く） | 0.12.0（Debian 0.12.0-3+b2） | `/usr/share/openocd/scripts/target/zynq_7000.cfg`（5a92fb5d41149a32）、`interface/ftdi/digilent-hs1.cfg`（f6cb3aff132fb6ed）、`board/digilent_zedboard.cfg`（412747f7d82ccc37） |
| openFPGALoader | 0.13.1（host） | `--help`、`--list-fpga`（`0x03722093 xilinx zynq xc7z010`）、`--list-boards`（`zybo_z7_10 digilent`、初代 ZYBO の行は無い） |
| QEMU | v10.0.11（host の Debian の版と同じ tag） | `hw/arm/xilinx_zynq.c`（cab3322dad698895）、`hw/arm/boot.c`（f2cd884f4a25b2f3）、`hw/arm/virt.c`（25ba8b095c0999cc）、`hw/misc/zynq_slcr.c`（9ff65ce3dc0aee2f）、`hw/char/cadence_uart.c`、`hw/net/cadence_gem.c`、`hw/sd/sdhci.c`、`hw/intc/arm_gic.c`、`hw/timer/a9gtimer.c`・`arm_mptimer.c` |

- 版 3 で読んだ物（2026-10-23 P2）: tree の `src/rtld/rtld.c`（arch の枝、`layout_static_tls`、riscv64 の relocation）、`src/rtld/entry-*.S`・`tlsdesc-*.S` の有無、`userland/base/libc/static-tls.c`・`m68k-read-tp.c`・`posix.c`（`uname`）、`include/uapi/tls.h`（variant）・`stat.h`・`time.h`・`types.h`、`src/kern/elf.c`（`load_static_tls` は amd64・i386・m68k だけ）、`src/kern/boot.c`（`init=` の boot parameter）、`platform/riscv32/vmunix.mk`、`platform/riscv64/userland.mk`・`rootfs.mk`、`toolchain/llvm/sysroot.mk`（compiler-rt の manifest）・`patches/0001-add-zedbsd-x86-target.patch`（PIE・`-dynamic-linker`）、`src/hal/mips/fpu.c`・`task.c`・`selftest.c`（sync・forget・`check_float`）、`include/hal/hal.h`（`hal_cpu_idle`・PAGE の flag・`hal_space_prot_query`）。外は review が取った物（同じ SHA-256、scratchpad `refs/qemu/`・`refs/linux/`）を読み直した: QEMU v10.0.11 `hw/arm/boot.c`（f2cd884f4a25b2f3: 930〜1145 行の direct boot、`bootloader[]`、`do_cpu_reset`）、`hw/arm/virt.c`（25ba8b095c0999cc: PSCI の conduit、`has_el2`/`has_el3`、`board_id`）、`hw/arm/xilinx_zynq.c`（cab3322dad698895: `loader_start = 0`、`board_id 0xd32`、`write_board_setup`）、`hw/misc/zynq_slcr.c`（9ff65ce3dc0aee2f: reset の値、`PSS_IDCODE` は read only で reset の値無し）、`hw/dma/xlnx-zynq-devcfg.c`（e31b10cff1129a1a）、Linux v6.12 `drivers/fpga/zynq-fpga.c`（ad19cfeb81e59aad）・`arch/arm/mm/proc-v7.S`・`proc-v7-2level.S`・`arch/arm/Kconfig`。版 3 で新しく取った物（scratchpad `refs3/`、tree に入れない）: Linux v6.12 `arch/arm/mm/fsr-2level.c`（36a135145951b7ec）、`arch/arm/kernel/entry-armv.S`（2134f86e4468a75d）、`entry-header.S`（a7c61e7d7f0749df）、`arch/arm/vfp/vfphw.S`（33fe2f65777448cd）。
- UG585（Zynq-7000 TRM）は PDF を取っていない。この文書で「UG585」と書く事実は、device tree・Linux・U-Boot・QEMU の source に現れる値から確かめた物で、表に無い事（CPU1 の BootROM の待ち方の細部、USB の VBUS の配線）は **未確認** と明記した。
- 確認の水準: build・QEMU・実機は何も走らせていない（設計の Phase）。QEMU の machine と CPU の有無は `qemu-system-arm -M help`・`-cpu help` で確かめた（§1.4）。
- 版 2 で host で確かめた事（2026-10-23 P1、板には触れていない）: `openocd` 0.12.0、`openFPGALoader` 0.13.1、`dtc`/`fdtput` 1.7.2、`arm-linux-gnueabihf-gcc` 14.2、`gdb-multiarch` 16.3 が入っている。`mkimage`（u-boot-tools）と openXC7（`yosys`・`nextpnr-xilinx`）は入っていない。共有の `build/llvm/bin/clang --print-targets` に arm が無い（aarch64・mips・ppc・riscv・x86 だけ）。`/dev/serial/by-id/` に `usb-Digilent_Digilent_Adept_USB_Device_210279785182-if00-port0`（JTAG、今の ttyUSB0、group plugdev）と `-if01-port0`（UART、今の ttyUSB1、group dialout）。user awe は plugdev に入っており、dialout は次の login から。

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

- 版 2: WS286 の起動は JTAG（§4）。boot mode の jumper が JTAG の時、BootROM は何も読まずに DAP を有効にして CPU0 を待たせる（SLCR `BOOT_MODE`（`0xF800025C`）[2:0] = 0 = `ZYNQ_BM_JTAG`、U-Boot `hardware.h`）。CPU1 は下の BootROM の待ちのまま。**WS286 は 1 CPU で走り、CPU1 は起こさない**（下の 2 つの方法の実装は後の WS。HAL は CPU1 が BootROM の `wfe` の loop に居る前提で `0xFFFFFFF0` に書かない）。
- 以下は版 1 の SD 起動の時の記述（後の WS の参考）: BootROM → BOOT.BIN の最初の partition（FSBL か U-Boot SPL）→ U-Boot（§4）。CPU0 だけが走り、CPU1 は BootROM の待ち loop に残る。**CPU1 の起こし方（実機）は 2 つ**:
  1. **OCM の `0xFFFFFFF0` に入口の物理 address を書き、`dsb; sev`**（BootROM が CPU1 を `wfe` で待たせ、起きたら `0xFFFFFFF0` を読んで飛ぶ、という UG585 6.3.9 の手順。版 3: p002 で実機の BootROM の code を読んで確かめた（§4.8: `0xFFFFFFF0` が loop 自身の address でなくなるまで `wfe`、変われば I-cache・分岐予測を invalidate してその値へ）。U-Boot・FSBL はこの loop に手を触れない）。
  2. Linux の方法（`platsmp.c` `zynq_cpun_start`、`slcr.c` `zynq_slcr_cpu_stop/start`）: SLCR を unlock（`0xF8000008` = 0xDF0D）、`A9_CPU_RST_CTRL`（`0xF8000244`）で CPU1 を reset（bit1 RST、bit5 CLKSTOP）、物理 0 に「`ldr r0, [pc, #0]; bx r0; .word 入口`」の 3 word を置いて cache を書き出し、reset を外す。1 が効かない時の fallback。
- QEMU `xilinx-zynq-a9` は `-smp 2` まで（`ZYNQ_MAX_CPUS 2`）。ELF を `-kernel` で渡すと `is_linux = 0`（`boot.c` `arm_load_elf`）で **2 つの CPU が同時に ELF の入口に入る**（`do_cpu_reset` の `!info->is_linux` の枝）。QEMU の SLCR の model は `A9_CPU_RST_CTRL` を書いても CPU を reset しない（`zynq_slcr.c` 471 行: ただの store）。版 3（L1）: zynq の machine は自分で DTB を作らない（`get_dtb` が無い）が、`-dtb FILE` を渡せば ELF の時も DTB を `loader_start` = PA 0 に置き `-append` を `bootargs` に書く（`boot.c` 957〜976・630 行）。ただし ELF の時は `r2` を設定しない（`do_cpu_reset` の `!is_linux` の枝は pc だけ）。WS286 の QEMU zynq の DTB の置き方は §4.6。→ **QEMU では、CPU1 は自分で入口に来て HAL の park loop で待ち、CPU0 が release word を書いて `sev` する**（§3.7）。
- QEMU `virt`（32 bit、`-cpu cortex-a15`）: RAM は **物理 `0x40000000` から**（`virt.c` `[VIRT_MEM] = { GiB, … }`、PA 0〜`0x07FFFFFF` は flash）、GICv2 `0x08000000`（dist）/`0x08010000`（cpu）、PL011 `0x09000000`、fw_cfg `0x09020000`、ARMv7 の generic timer（62.5 MHz）、virtio-mmio、二次 CPU は PSCI の `CPU_ON` で起こす。LPAE は使わない（短い descriptor で走る）。
  - 版 3（C2）: QEMU は **ELF の kernel には `r0`〜`r2` を設定せず `-initrd` も読まない**（`boot.c` 956 行「raw images are linux kernels, and ELF images are not」、1042 行の `if (is_linux)`）。**raw の image**（`vmunix.bin`）を `-kernel` に渡すと Linux の起動の約束になる: image を `loader_start + 0x10000` = **`0x40010000`** に置き（36 行 `KERNEL_LOAD_ADDR`、997 行）、initrd を `loader_start + MIN(ram/2, 128 MiB)`（512 MiB なら `0x48000000`、image の終わりより上、page 揃え、1035〜1040 行）に、DTB をその後ろ（4 KiB 揃え、1106 行）に置いて `/chosen` に `bootargs`・`linux,initrd-start`・`-end` と `memory@40000000` を書き（`arm_load_dtb`、617・630・640 行）、`0x40000000` の小さな loader（`bootloader[]`、`r0 = 0`・`r1` = board id（virt は `-1`、2459 行）・`r2` = DTB の物理）から入口へ飛ぶ。WS286 の virt は **この道を採る**（§4.6）。
  - 入る時の CPU: virt は既定（`secure=off`・`virtualization=off`）で EL3・EL2 を外す（`virt.c` 2264・2267 行）ので、**Non-secure の SVC** で入る（`do_cpu_reset` の `arm_emulate_firmware_reset(cs, 1)`）。zybo の実機（JTAG）と QEMU zynq（ELF）は Secure の SVC。HAL は両方で動く書き方にする（secure 専用の register を virt で書かない、§3.5・§3.7）。
  - 版 3（M2）: PSCI の conduit は既定で **HVC**（`virt.c` 2186〜2192 行: `virtualization=on` の時だけ SMC）。HAL は DTB の `/psci` の `method`（`"hvc"`/`"smc"`）で選ぶ。EL2 が無くても QEMU が PSCI の呼び出しとして扱う（`fdt_add_psci_node` が method を書く。HVC が QEMU の PSCI に届く事は p004 の `reboot` で確かめる）。

### 1.4 QEMU の machine の有無（確認済み、2026-10-23）

`qemu-system-arm --version` = 10.0.11（Debian 1:10.0.11+ds-0+deb13u1）。`-M help` に `virt`（10.0）と `xilinx-zynq-a9`（Cortex-A9、Zynq 7000）、`-cpu help` に `cortex-a9`・`cortex-a15`・`cortex-a7`。zynq の model に有る物: SLCR、GIC（PL390 の register 互換）、private/global timer、Cadence UART ×2、Cadence GEM ×2、SDHCI ×2（capabilities 0x69ec0080）、ChipIdea ×2、QSPI、TTC、devcfg（`0xF8007000`、`hw/dma/xlnx-zynq-devcfg`）、OCM。無い物（unimplemented）: GPIO、I2C、CAN、SWDT、DDRC、SMC。
版 3（H1）: 版 2 の「PL310 の model は無い」は誤り。QEMU zynq は `l2x0` の model を `0xF8F02000` に持ち（`xilinx_zynq.c` 271 行）、`CACHE_ID` は ARM の `0x410000C8`（`arm_l2x0.c` 30・75 行）を返す。model は register の箱だけで cache の振る舞いは無い。MIDR も実機と同じ `0x413FC090`（r3p0、`xilinx_zynq.c` 58・223 行）。→ PL310 と MIDR は判別に使えない。
**QEMU か実機かの判別（版 3）: SLCR の `PSS_IDCODE`（`0xF8000530`）が 0 なら QEMU**。QEMU の `zynq_slcr.c` は `PSS_IDCODE` を read only にするだけで reset の値を入れない（135・455 行、325〜378 行の reset に無い）ので 0 を読む見込み（QEMU での実測は p005 の最初の T1 の起動）。**実機の値は `0x13722093`**（p002 の probe、§4.8・[phase002](phase002/phase.md)）。BSP は下位 28 bit を `0x3722093` と比べ（上位 4 bit は silicon の版）、0 でも一致でもなければ `ZYBO IDCODE UNKNOWN 0x…` を出し実機として続ける。実機にも PL310 が有り（`CACHE_ID` 0x410000c8、SPL の後は無効、§4.8）、L2 の扱い（§3.11）は両方で同じ。判別の結果は boot の 1 行 `ZYBO PLATFORM qemu|board idcode=0x…` で出す。DTB の property での判別（`/chosen/zedbsd,platform`）は道具が書き忘れると誤るので主にしない（表示の照合だけ）。判別を使う所は 2 つだけ: timer の周波数（§3.6、QEMU は 100 MHz）と devcfg の QEMU 用の待ちの省略の許可（§5.6、boot parameter と二重の条件）。

## 2. toolchain と sysroot

### 2.1 LLVM（**Q1 の許可が要る toolchain の変更**。P1 は適用しない）

- 今の `toolchain/llvm/llvm.mk` 361 行: `-DLLVM_TARGETS_TO_BUILD='PowerPC;AArch64;RISCV;X86;Mips'`。**`ARM` が無い**（`--print-targets` に arm が出ない見込み。P1 は共有の `build/llvm` を走らせていない: 確認は Q1）。
- 案 1（必須、kernel の build に要るのはこれだけ）: targets に `ARM` を足す（`'PowerPC;AArch64;RISCV;X86;Mips;ARM'`）、`version.mk` の `ZEDBSD_LLVM_PATCH_LEVEL` を `zedbsd10` → `zedbsd11`（install の stamp と cache asset の名前を変えるため。memory の「LLVM patch level needs new cache」: rev-0 の cache asset の upload と digest の更新を同じ commit で）。大きさ: ARM の backend の build は AArch64 と同程度（数分〜十数分、`build/llvm-build` が数百 MB 増える）。
- 案 2（userland を作る段で要る、p007。版 2: **案 1 と同じ 1 回の作り直しで入れる事を勧める**、§10）: `0001-add-zedbsd-x86-target.patch` に同じ形で足す:
  - `clang/lib/Basic/Targets.cpp` の `case llvm::Triple::arm:`・`armeb`・`thumb`・`thumbeb` の OS の switch に `case llvm::Triple::ZedBSD: return std::make_unique<ZedBSDTargetInfo<ARMleTargetInfo>>(Triple, Opts);`（`ARMleTargetInfo`、big endian は作らない）。
  - `clang/lib/Driver/ToolChains/ZedBSD.cpp` の linker の `-m`: `arm` → `armelf_linux_eabi`（ld.lld の ARM の emulation 名）。`-z max-page-size=4096` は aarch64 と同じ枝に `arm` を足す。
  - 同じ file の ARM の既定: `-mfloat-abi=hard`、`-mfpu=neon-vfpv3`、`-mtp=cp15`（TLS を `TPIDRURO` で読む）、`-marm`（Thumb-2 は使わない、§3.9）。
  - `TripleTest.cpp`・`clang/test/Preprocessor/zedbsd.c`: `armv7-unknown-zedbsd-eabihf` で `__ZEDBSD__`・`__arm__`・`__ARM_ARCH 7`・`__ARM_PCS_VFP`。
- triple: **`armv7-unknown-zedbsd-eabihf`**（環境名 `eabihf` で hard-float を名乗る。`arm-unknown-zedbsd` に `-mfloat-abi=hard` を付ける案は、triple から ABI が読めないので採らない）。

### 2.2 kernel の build（platform/armv7、案 1 だけで足りる）

`--target=armv7-unknown-none-eabi -D__ZEDBSD__=1 -DHAL_ARCH_ARMV7 -DKERN_USER_ABI_ARMV7 -march=armv7-a -mcpu=cortex-a9 -marm -mfloat-abi=soft -mfpu=none -mno-unaligned-access -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror`、`ld.lld -m armelf`。
- kernel は浮動小数点を使わないので soft（VFP の register は user の物。HAL の保存・復元の asm だけ `.fpu neon-vfpv3`）。
- `-mno-unaligned-access`: SCTLR.A=0 でも device memory の非整列 access は abort になるので、kernel の code では非整列を生成させない。
- 版 3（H5）: `-DKERN_USER_ABI_ARMV7` は `platform/armv7/vmunix.mk` の CPPFLAGS に置く（`platform/riscv32/vmunix.mk` 13〜15 行の `-DKERN_USER_ABI_RISCV32` と同じ形）。`include/uapi/stat.h` の 91 行の枝に `KERN_USER_ABI_ARMV7` を足し、同じ所に `#if defined(HAL_ARCH_ARMV7) && !defined(KERN_USER_ABI_ARMV7)` → `#error "armv7 builds define KERN_USER_ABI_ARMV7"` の 1 行を置く。kernel（vmunix.mk）と user（sysroot の `HAL_ARCH_ARMV7` と `-DKERN_USER_ABI_ARMV7`、`platform/armv7/userland.mk`）の両方が `HAL_ARCH_ARMV7` を定義するので、どちらかで macro を忘れると build が止まる。両側の stat は同じ枝の `_Static_assert`（timespec 16・stat 92）で固定される。注: 読み直すと、macro を忘れた側は `#else` の枝の `_Static_assert(sizeof(struct timespec) == 12)` で build が落ちる見込み（`time_t` は `int64_t`（`types.h` 79 行）で AAPCS は 8 byte に揃えるので timespec は 16）なので、review の「黙って食い違う」は起こりにくい。それでも原因の分かる error にするため上の 1 行を置く。
- 版 3（M1）: **EABI の helper**（A9 は ARM mode の `sdiv`/`udiv` を持たない。kernel は `-mcpu=cortex-a9` なので virt の A15 でも同じ呼び出しが出る）。kernel では自分の code（Zlib）で持つ: 32 bit は `src/libc/int64.c` の `HAL_ARCH_ARMV7` の節の C（`uint64_t` の戻り値は AAPCS で r0:r1 なので「商 | 余り << 32」で 2 つを返せる）、64 bit の divmod と shift は `src/hal/armv7/aeabi.S`（C の helper を呼んで余りを r2:r3 に積む）。mem の 4 つは `hal_memcpy`/`hal_memset` か kernel の `memcpy`/`memmove`/`memset` へ渡す asm の薄い wrapper（4/8 の別名は同じ実装）。0 での除算は `__aeabi_idiv0`/`__aeabi_ldiv0` → `HAL_FATAL`。

| symbol | 引数 → 戻り | 注 |
| --- | --- | --- |
| `__aeabi_uidiv`・`__aeabi_idiv` | (n r0, d r1) → 商 r0 | |
| `__aeabi_uidivmod`・`__aeabi_idivmod` | (n r0, d r1) → 商 r0・余り r1 | C で `uint64_t` を返す |
| `__aeabi_uldivmod`・`__aeabi_ldivmod` | (n r0:r1, d r2:r3) → 商 r0:r1・余り r2:r3 | asm |
| `__aeabi_llsl`・`__aeabi_llsr`・`__aeabi_lasr` | (v r0:r1, shift r2) → r0:r1 | asm か C（`uint64_t` の引数・戻りは r0:r1） |
| `__aeabi_memcpy{,4,8}`・`__aeabi_memmove{,4,8}` | (dest, src, n) | memcpy と同じ順。戻り値は無い |
| `__aeabi_memset{,4,8}` | **(dest, n, c)** | memset(dest, c, n) と **順が違う** |
| `__aeabi_memclr{,4,8}` | (dest, n) | |
| `__aeabi_idiv0`・`__aeabi_ldiv0` | — | `HAL_FATAL` |

  LLVM の ARM の codegen が `memcpy` 等を `__aeabi_*` に落とすか（`armv7-unknown-none-eabi` の kernel と `…-zedbsd-eabihf` の user で）は LLVM の source を読んでいない（推測）。p004 の最初の link の `llvm-nm -u` の結果で表を確定し、出ない symbol は作らない。host 試験（`plan/ws286/tests/aeabi-host-test.c`）は C の部分の算術（境界: 0x80000000/-1、0xFFFFFFFF/1、64 bit の 2^63）を確かめ、register の約束（r0:r1・r2:r3・memset の順）は target の HAL self-test `AEABI PASS`（§7.6）で確かめる。
- LTO（Makefile の既定）: `-mcpu` と `-mfloat-abi` は module の属性で残るので riscv32 と同じで通る見込み。`.ARM.exidx`・`.ARM.attributes` は linker script で `/DISCARD/`（exidx）と保持（attributes、ld.lld が要る）。
- 版 3（C1・C2）: vmunix.ld の link の場所は **platform ごと**（`platform/armv7/<platform>.mk` が `ARMV7_PHYS_BASE` と `ARMV7_KERNEL_PHYS` を `-D` と linker script の `--defsym` で渡す。VMA = `0x80000000 + (KERNEL_PHYS - PHYS_BASE)`、LMA = `KERNEL_PHYS`、`ENTRY` は物理の `_start`）:

| platform | `ARMV7_PHYS_BASE`（RAM の先頭） | `ARMV7_KERNEL_PHYS`（LMA） | link の VMA | 置く人 |
| --- | --- | --- | --- | --- |
| `zybo` | `0x00000000` | `0x00100000` | `0x80100000` | jtag-boot.sh（raw か ELF、§4.8）、QEMU zynq（ELF） |
| `armv7-virt` | `0x40000000` | `0x40010000` | `0x80010000` | QEMU の Linux の約束（raw、§1.3・§4.6） |

  - `.text.boot`（`_start`）を image の先頭に置き、`llvm-nm` の `_start` が VMA の先頭である事を §7.1 で確かめる（raw の image は先頭から入る）。`vmunix.bin`（`llvm-objcopy -O binary`）は BSS を含まないので、locore は BSS を 0 にする前に `r2`（DTB）と、DTB の `/chosen` の initrd の範囲が `[_start, _end)` に重ならない事を物理の address で確かめ、重なれば UART/ramfb に何も出せないので `wfi` の loop で止まる（gdbstub で pc を見る）。QEMU の virt は initrd を 128 MiB 目に置くので 512 MiB では重ならない。
  - U-Boot の legacy uImage `uvmunix`（64 byte の header + CRC32、`platform/armv7/tools/make-uimage.py`、load=entry=`ARMV7_KERNEL_PHYS`）は SD の起動の WS で作る（WS286 は使わない）。

### 2.3 sysroot と libc（`toolchain/llvm/sysroot.mk`、**Q1 の変更**、p007 の前）

`ZEDBSD_BUILD_SYSROOT` の呼び出しを 1 つ足す: `(…,armv7,armv7-unknown-zedbsd-eabihf,-march=armv7-a -mcpu=cortex-a9 -marm -mfloat-abi=hard -mfpu=neon-vfpv3 -mtp=cp15 -mno-unaligned-access,HAL_ARCH_ARMV7,src/libc/crt/crt0-armv7.S,src/libc/crt/crt1-armv7.S,armv7,user.ld,ARM,-DKERN_USER_ABI_ARMV7,$(ZEDBSD_SYSROOT_ARMV7_LIBC_SOURCES),,$(ZEDBSD_SYSROOT_ARMV7_COMPILER_RT_SOURCES),$(ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_SOURCES))`。
- `crt0-armv7.S`・`crt1-armv7.S`（aarch64 の写しを AAPCS に: `_start` は sp から argc/argv/envp、`__signal_restorer` は sigreturn の syscall）、`src/libc/setjmp-armv7.S`（r4–r11・sp・lr と d8–d15、`ZEDBSD_SYSROOT_ARMV7_LIBC_SOURCES`）。版 3（H4）: `__syscall6` は crt0 でなく tree の形どおり `userland/base/libc/syscall-armv7.S`（`syscall-aarch64.S` の形: r7 = 番号・`svc #0`、r0〜r5 の引数、6 つ目の引数は AAPCS で stack から r5 へ読む）。
- long double は AAPCS で 64 bit（double と同じ）なので `softfloat128.c` は要らない。hard-float なので `softfloat.c` も要らない。
- compiler-rt の builtins（版 3 で整理）: 整数の共通の表（`ZEDBSD_SYSROOT_LLVM_BUILTIN_NAMES`、既に検証済みの LLVM の source から取る manifest、license は Apache-2.0 WITH LLVM-exception で既存の監査の境界と同じ）に加えて、`ZEDBSD_SYSROOT_ARMV7_LLVM_BUILTIN_SOURCES` に ARM の `arm/aeabi_idivmod.S`・`aeabi_uidivmod.S`・`aeabi_ldivmod.S`・`aeabi_uldivmod.S`・`aeabi_memcpy.S`・`aeabi_memmove.S`・`aeabi_memset.S`・`aeabi_memcmp.S`・`aeabi_div0.c` と、VFPv3 に命令の無い 64 bit 整数と浮動小数点の変換（`fixdfdi.c`・`fixunsdfdi.c`・`floatdidf.c`・`floatundidf.c`・`fixsfdi.c`・`fixunssfdi.c`・`floatdisf.c`・`floatundisf.c`。`__ARM_EABI__` で `__aeabi_d2lz` などの別名が付く）。hard-float なので `aeabi_dcmp.S`・`aeabi_fcmp.S` は要らない見込み。**最終の list は p007 の最初の link の `llvm-nm -u`**（版 2 の「p009」は誤記）。`clear_cache.c` は入れない（user は `ic`/`dc` を持たず、`cacheflush` の syscall は JIT の WS、§3.13）。sysroot の名前は `build/armv7/sysroot`。
- 版 3（H4）**userland の形は riscv64 と同じ動的 PIE**（`platform/riscv64/userland.mk` の注: 「position-independent executables that load /lib/libc.so through /lib/ld.so … Every module's TLS is dynamic (general-dynamic …)」。clang の zedBSD の driver も既定は `-pie` と `-dynamic-linker /lib/ld.so`、patch の 305〜306・366〜367 行）。理由: (1) kernel の `load_static_tls` は amd64・i386・m68k だけで、他の arch の PT_TLS を持つ static の image は `ENOEXEC`（`src/kern/elf.c` 1383〜1389 行）、(2) rtld の `layout_static_tls` も variant I の arch では空（rtld.c 2103〜2114 行、ARM も variant I）、(3) libc は TLS（errno など）を持つ。よって **WS286 の userland は rtld の ARM 版が要る**。足す物:
  1. `platform/armv7/userland.mk`（riscv64 の写し）: `-ftls-model=global-dynamic`、ARM は TLS の dialect の flag を渡さない（LLVM の ARM は traditional の `__tls_get_addr` だけと推測。p007 の最初の compile で確かめる）、`-fPIC`、`-DHAL_ARCH_ARMV7 -DKERN_USER_ABI_ARMV7 -DKERN_DYNAMIC_LIBC`、ld.lld `-m armelf_linux_eabi -z max-page-size=4096`。
  2. rtld の ARM 版: `src/rtld/rtld.c` の `RTLD_MACHINE EM_ARM`・`RTLD_DATA ELFDATA2LSB`・`RTLD_RELATIVE R_ARM_RELATIVE`、`src/rtld/elf.h` の `R_ARM_*` の番号（ABS32 2、TLS_DTPMOD32 17、TLS_DTPOFF32 18、TLS_TPOFF32 19、COPY 20、GLOB_DAT 21、JUMP_SLOT 22、RELATIVE 23。値は p007 で LLVM の `ELFRelocs/ARM.def` から Values referenced で引く）、relocation の switch（ARM は **REL（暗黙の addend）**なので i386 の枝の読み方、TLS の扱いは riscv64 の枝: DTPMOD は module の番号、DTPOFF は block の中の offset（bias 0）、`TLS_TPOFF32`（initial-exec）は riscv64 と同じく拒む）、thread pointer の読み（6286〜6290 行の並びに `mrc p15, 0, %0, c13, c0, 3`）、`src/rtld/entry-armv7.S`（`entry-aarch64.S` の形: sp を渡して C の入口、戻り値の入口へ `bx`）。lld の ARM が TLS の GD→LE の relax をしない事（推測）は p007 で `llvm-readelf -r` に `R_ARM_TLS_TPOFF32`・LE の reloc が無い事で確かめる。
  3. libc の arch の file と枝: `syscall-armv7.S`、`setjmp-armv7.S`、`static-tls.c` の `static_thread_pointer`（229〜239 行の並びに TPIDRURO）、`posix.c` の `uname`（5376〜5387 行の並びに `#elif defined(__arm__)` → `"armv7"`）、`pthread.c` の spin の hint（`yield`）。
  4. ELF・ABI: 下の 2 項（elf.c、stat.h）と、kernel の exec の TLS の初期値（variant I の TCB 8 byte の約束は rtld が持つ。kernel は動的の image に static TLS を作らない、今の elf.c のまま）。
  見積もり（版 3）: rtld の ARM 版 6〜8 LW、libc の arch の file・userland.mk・crt・abitest で 8〜10 LW（§8 の p007）。`uname -m` は `armv7`（zedBSD の platform と triple の名前。Linux の `armv7l` の `l` は little endian の印で、big endian の port を作らない zedBSD では情報が無い。i386 の port も `i686` でなく `i386` を返す）。
- ELF: `include/kern/elf.h` に `EM_ARM`（40）、`src/kern/elf.c` に `HAL_ARCH_ARMV7`（ELF32、little endian、`EF_ARM_ABI_FLOAT_HARD`（0x400）の無い実行 file は拒む）。
- `include/uapi/stat.h`: AAPCS は 64 bit の member を 8 byte に揃えるので `KERN_USER_ABI_RISCV32 || PPC32 || MIPS32` の枝に `KERN_USER_ABI_ARMV7` を足す（timespec 16 byte・stat 92 byte）。
- userland の `platform/armv7/user.ld`: arm64 の写し、`. = 0x00400000`、`.ARM.exidx` は `/DISCARD/`（例外の unwind は C++ の WS で見直す）。

### 2.4 大きさの見積もり

LLVM の作り直し（Q1、案 1 と案 2 を 1 回で）: build の時間は 4 並列で 30〜60 分（版 3 で §10 の値に揃えた、未計測）。tree の差分は llvm.mk 1 行 + version.mk 2 行 + cache の digest + 案 2 の patch 60〜80 行（`-mtp=cp15` の既定を `ZedBSD.cpp` に置けず `Driver/ToolChains/Arch/ARM.cpp` の判定に足す事になれば増える: patch を書く時に確かめる、L6）。sysroot は sysroot.mk に 15 行（呼び出しと ARMV7 の manifest 2 つ）+ crt0/crt1/setjmp/syscall の 4 file（計 200 行）。rtld の ARM 版は rtld.c に 150〜250 行 + entry 1 file。

## 3. ARMv7-A の HAL（`src/hal/armv7/`）

### 3.1 directory

```
src/hal/armv7/
  locore.S        入口、CPU の初期化、page table の初期化、MMU on、二次 CPU の入口と park
  vectors.S       例外 vector と stub（SRS/CPS/RFE で統一 frame）
  dispatch.S      context switch、user への戻り（monitor を閉じる dummy の strex、§3.3）、VFP の保存・復元
  selftest.c      版 3: AEABI・ATOMIC・PAGING・VFP の self-test（§7.6、mips の selftest.c の形）
  aeabi.S         版 3: 64 bit の divmod・shift と mem の __aeabi_* の wrapper（§2.2）
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
| `0x00001000`〜`0x7FFFFFFF` | user の page | user（TTBR0、2 GiB、space ごとの L1 は 2048 entry = 8 KiB、8 KiB 揃え） | 4 KiB の small page。L2 の table は **2 MiB ごとに 1 page**（hardware の 2 つ + 影の 2 つ、下の H2） |
| `0x80000000`〜`0xBFFFFFFF` | `PHYS_BASE`〜`PHYS_BASE + 0x3FFFFFFF`（版 3、C1） | **direct map**（RAM。ZYBO は 512 MiB（Z-7020 の 1 GiB まで）、virt は `-m` の 512 MiB。1 GiB を超える RAM は報告しない）、kernel image は §2.2 の VMA | 1 MiB の section、Normal WBWA、Shareable。image の text は section を L2 に割って RX、rodata R、残り RW（W^X を boot で確かめる、riscv32 と同じ）。**zybo は最初の 1 MiB（VA `0x80000000`〜`0x800FFFFF` = PA 0〜`0xFFFFF`）を張らない**（M9、§4.8 の実測: OCM・external abort の穴・高位 OCM の alias） |
| `0xC0000000`〜`0xDFFFFFFF` | 任意 | **device 窓**: `hal_space_map_device`、`hal_pmem_map_uncached`（512 MiB） | 4 KiB の page、L2 の table は要る時に pmem から割る（kernel の L1 は 1 つなので user の L1 の写しに届く問題は無い: TTBR1 が担うため） |
| `0xE0000000`〜`0xFFFFFFFF` | 同じ（identity、zybo だけ） | PS の IOP・SLCR・MPCore・OCM・QSPI linear（zybo）。virt はこの範囲に何も張らず、`0x08000000`〜`0x0A000000` の device を device 窓に張る | 1 MiB の section、Device（TEX=0,C=0,B=1）。OCM（`0xFFF00000` の section）は Strongly-ordered（CPU1 の release word） |

- 版 3（C1）: `PHYS_BASE` は §2.2 の platform ごとの値（zybo 0、virt `0x40000000`）。`hal_pmem_to_kernel(p) = p - PHYS_BASE + 0x80000000`（`PHYS_BASE ≤ p < PHYS_BASE + direct map の大きさ` の時だけ、他は NULL）、逆は `v - 0x80000000 + PHYS_BASE`。virt の RAM の `0x40000000`〜`0x5FFFFFFF` は VA `0x80000000`〜`0x9FFFFFFF` に来て、device 窓と重ならない。kernel が MMU on の前に走る物理の場所（zybo `0x00100000`、virt `0x40010000`）は TTBR0 の範囲（< 2 GiB）なので、locore は一時の identity の section をその 1 MiB に張って高い VA へ飛び、飛んだ後に外す（boot 専用の TTBR0 の L1）。pte の host 試験は `PHYS_BASE` 0 と `0x40000000` の両方で流す。
- TTBR1 は kernel の L1（16 KiB 揃え、上半分の 2048 entry だけ使う）。TTBCR.N=1 で `0x80000000` 以上が TTBR1。user の L1 は 8 KiB（N=1 の TTBR0 は 2048 entry）。
- TEX remap は使わない（SCTLR.TRE=0）。属性: Normal WBWA = TEX=001,C=1,B=1、Normal non-cacheable（uncached の DMA の buffer）= TEX=001,C=0,B=0、Device = TEX=000,C=0,B=1、Strongly-ordered = 全部 0。`HAL_SPACE_WC` は Normal non-cacheable に落とす。
- SCTLR.AFE = 1（Access Flag を使う、AP[0] が AF）、SCTLR.A = 0、SCTLR.V = 0 + VBAR（§3.3）。domain は DACR で domain 0 を client だけにする（domain fault は起きない前提、§3.3 の分類には残す）。
- 版 3（H3）: **1 CPU でも Linux の SMP の kernel と同じ設定にする**（Procedure referenced: Linux v6.12 `arch/arm/mm/proc-v7.S` `__v7_ca9mp_setup`（ACTLR の bit 0 FW と bit 6 SMP を、立っていなければ立てる）、`proc-v7-2level.S` `TTB_FLAGS_SMP`（IRGN_WBWA・S・NOS・RGN_OC_WBWA）と `cpu_v7_set_pte_ext` の SMP の時の `flush_pte` の nop）。zybo の locore は MMU と D-cache を有効にする前に (1) SCU を有効にする（`SCU_CTRL` `0xF8F00000` の bit 0 を read-modify-write で立て、他の bit は保つ。p002 の実測は SPL の後 `0x00000002` = 無効、§4.8）、(2) `ACTLR` の SMP|FW を立てる（JTAG の流れは Secure の SVC なので書ける）。TTBR は IRGN=WBWA・RGN=WBWA・S=1・NOS=1（Multiprocessing Extensions では IRGN は bit 6 と bit 0 に分かれる、L6）。pte を書いた後は `dsb` だけ（clean は要らない、Linux の SMP の道と同じ）。
  - virt（A15、Non-secure の SVC で入る）では SCU も ACTLR も触らない（QEMU は coherent な model で差が出ない。A15 の ACTLR を NS から書くと NSACR 次第で未定義になり得るため）。この 2 つは bsp-zybo の `bsp_cpu_early_setup()`（MIDR の primary part `0xC09` を確かめてから）に閉じ込める。
  - 選ばなかった案: UP の Linux と同じ「Shareable を外し pte を clean する」。後の SMP の WS で全部の属性と pte の書き方を変え直す事になり、Zynq の Linux が実際に通る道（SMP の kernel）でもない。
  - self-test（§7.6）: `ATOMIC PASS`（Normal・Shareable の word に `ldrex/strex` の加算 10000 回が、各回 16 回以内の retry で終わる）、boot の 1 行に `ACTLR`・`SCU_CTRL`・`ID_MMFR3`（[23:20] coherent walk）の値を出す。QEMU の A9 の ACTLR は書いても読みが変わらない見込み（推測）なので、ACTLR の値の判定は実機だけ。
- 版 3（H2）**software の page の状態は Linux の arm と同じ影の table に置く**（短い descriptor の small page の bit は全部 hardware が使う）。L2 の 1 page（4 KiB）= `+0x000` 影の table 2 つ（VA の 2 MiB 分、512 entry × 4 byte）、`+0x800` hardware の L2 table 2 つ（1 KiB ずつ、1 KiB 揃え）。L1 の entry 2n と 2n+1 は page + `0x800` と + `0xC00` を指す。影の entry: PFN と `PRESENT`・`READ`・`WRITE`（論理）・`EXEC`・`USER`・`ACCESSED`・`DIRTY`・属性の番号（Normal/NC/Device）。hardware の entry は影から作る: AP は `WRITE && DIRTY` の時だけ RW、それ以外 RO（user/kernel は AP[1]）、AF = `ACCESSED`、XN = `!EXEC`。Procedure referenced: Linux v6.12 `proc-v7-2level.S` `cpu_v7_set_pte_ext`（Linux の pte を `[r0]`、hardware の pte を `[r0, #2048]` に書く）。`hal_space_destroy` は 2 MiB ごとの page と L1 の 8 KiB を返す。
  - 状態遷移（HAL の中で解く、kernel に出さない）: AF の fault → 影の `ACCESSED` を立て hardware の AF を 1。permission の fault（write）で影が `WRITE && !DIRTY` → `DIRTY` を立て hardware を RW にして同じ命令をやり直す。影が `!WRITE` → `PAGE_FAULT`（mode `WRITE`）を kernel へ。`hal_space_prot_query` で write を外す: 影の `WRITE` を落とし hardware を RO → `dsb` → TLB の invalidate → `dsb; isb` → 影の `DIRTY`・`ACCESSED` を返す（hardware は自分で DIRTY を立てないので、DIRTY でない page への store は必ず fault を通る。古い TLB の entry 経由の store は DIRTY の後でしか起きない）。
  - 選ばなかった案: TEX remap（SCTLR.TRE=1、PRRR/NMRR）で TEX[2:1] を OS に空ける。bit が 2 つしか空かず（WRITE と DIRTY と ACCESSED の 3 つに足りない）、ARM ARM の該当の記述を確かめていない。
- `hal_space_get_page_size(1)` = 4096、`(2)` = 1 MiB（section。user には使わない）。`hal_space_get_user_range` = `0x00001000`〜`0x80000000`。

### 3.3 例外

- VBAR（cp15 c12）に kernel の vector table（Security Extensions の有る A9 で使える。virt の A15 も）。
- 全部の例外を **SVC mode の統一 frame** で扱う。版 3（L3）: 入口は Linux の `vector_stub`/`svc_entry` の形（Procedure referenced: Linux v6.12 `arch/arm/kernel/entry-armv.S` の `svc_entry` の `SPFIX`: `tst sp, #4` → `subne sp, sp, #4`）にする: 各 mode の stub は per-CPU の 3 word の予備（mode の banked sp が指す）に r0・lr_<mode>（戻り先に直した値）・spsr_<mode> を置き、`cps #0x13` で SVC に移り、SVC の sp の bit 2 を見て 4 byte 下げてから（下げた事を frame に記録）、`push {r0-r12}`・予備の 3 word・`stmdb {sp, lr}^`（user の sp・lr）・lr_svc で frame を作る。frame は 20 word = 80 byte（r0–r12、sp_usr、lr_usr、lr_svc、pc、spsr、pad の印、予備）で 8 byte の倍数。user からの例外は task の kernel stack の頂上（8 byte 揃え）に積むので pad は付かない。kernel の中で sp が 4 byte 揃えの瞬間に来た例外だけ pad が付き、戻りで外す。C の handler は常に AAPCS の 8 byte 揃えで走る。
- 版 3（M9）: VBAR を設定し vector の page を張った直後に `cpsie a` で async abort の mask を外す（JTAG の入口は `cpsr` = 0x1D3 で A が立っている）。`cpsie a` の直後の数命令の間に来た async abort（SPL や JTAG の操作の名残）は `ARMV7 PENDING ASYNC ABORT DFSR=… DFAR=…` を出して 1 回だけ読み捨てる（その窓の印を per-CPU に置く）。それ以外の async abort は `MACHINE_CHECK`。
- 版 3（M7）: **例外からの戻りの全部**（kernel への戻り、user への戻り、context switch の後の最初の戻り）で、排他の monitor を dummy の `strex` で閉じる（Procedure referenced: Linux v6.12 `arch/arm/kernel/entry-header.S` の `svc_exit`・`restore_user_regs`: 「We must avoid clrex due to Cortex-A15 erratum #830321」で、frame の下の使わない word に `strex`）。A9 には `clrex` も有るが、virt の A15 と同じ code にするため Linux と同じ `strex` にする。user への戻りの前に、戻る spsr の mode が user で I・F が 0 である事を確かめ、違えば `HAL_FATAL`（Linux の `tst r1, #PSR_I_BIT | 0x0f`）。
- 分類（`int.c`、版 3（L2）で直した。符号は短い descriptor の FS[10,3:0]、名前と並びは Linux v6.12 `arch/arm/mm/fsr-2level.c` の `fsr_info`・`ifsr_info` の表に合わせた: Values referenced）:

| 例外 | 原因 | HAL_TRAP |
| --- | --- | --- |
| Data Abort | DFSR の FS（`fsr_info`）: translation `0b00101`（section）/`0b00111`（page）、access flag `0b00011`/`0b00110`、permission `0b01101`/`0b01111`、domain `0b01001`/`0b01011` → 影の table で HAL が解くか（§3.2）、`PAGE_FAULT`（mode は DFSR.WnR（bit 11）で `WRITE`/`READ`、address は DFAR）。alignment `0b00001` → `ALIGNMENT`（Linux の `fsr_info` は 3 番も「alignment」と呼ぶが ARMv5 の名残で、SCTLR.AFE=1 の ARMv7 では 3 番は section の access flag。この表は ARMv7 の意味で読む）。icache の maintenance `0b00100` → `MACHINE_CHECK`（WS286 は user の範囲の maintenance をしない）。external abort: 同期 `0b01000`、table walk の 1 段目 `0b01100`・2 段目 `0b01110`、非同期 `0b10110`（imprecise）→ `MACHINE_CHECK`。debug event `0b00010` → `DEBUG_POINT`。DFSR.CM（bit 13、cache maintenance の fault）が立てば WnR を見ず `MACHINE_CHECK`。DFSR.ExT（bit 12）は表示だけ |
| Prefetch Abort | IFSR の FS（`ifsr_info`）、address は IFAR、mode `EXEC`: translation・access flag・permission → `PAGE_FAULT`、debug event `0b00010`（`bkpt` の命令はここに来る）→ `BREAKPOINT`、external → `MACHINE_CHECK` |
| Undefined | user から来て FPEXC.EN=0 → §3.10 の lazy の復元をして同じ命令をやり直す（kernel に知らせない）。user から来て FPEXC.EN=1 → `ILLEGAL_INSN`（本当の未定義。LLVM の `__builtin_trap` の `udf` 系の符号もここ）。kernel から → `HAL_FATAL`。版 2 の「`bkpt` → `BREAKPOINT`」は誤り（ARMv7 の `bkpt` は Prefetch Abort の debug event） |
| SVC | syscall: 番号は r7、引数 r0–r5（`HAL_SYSCALL_ARGS` = 6）、戻り r0。kernel からの `svc` は無い（`HAL_FATAL`） |
| IRQ | GIC の ack → `hal_irq_*` の handler、timer の PPI は `kernel_timer_handler`、SGI は `kernel_cpu_notify_handler` / 停止 |
| FIQ | 使わない（CPSR.F は立てたまま。GIC の Group 0 を IRQ に出す: GICC_CTLR.FIQEn=0） |

- `kernel_user_return_handler()` は user への戻りの直前に IRQ を止めて呼ぶ（arm64 の `dispatch.S` と同じ形）。
- access flag と dirty の追跡（A9 は hardware で立てないので software）: 版 3（H2）で状態を §3.2 の影の table に置いた。遷移と `hal_space_prot_query` の順は §3.2。fault の解決は mips の M1〜M3 と同じく HAL の中で行い、kernel の fault handler には出さない。
- 版 3（M8）: erratum 775420（Cortex-A9 r2p2〜r3p0、「data cache maintenance operation which aborts」の deadlock、対処は「DSB before executing ISB if an abort may occur on cache maintenance」、Linux v6.12 `arch/arm/Kconfig`）: WS286 の HAL は fault し得る address（user の範囲）に MVA の cache maintenance をしない（`hal_dcache_*` は direct map と device 窓だけ）ので踏まない。後の `cacheflush` の syscall の WS で、その fixup の道の `isb` の前に `dsb` を置く規則として §3.11 に残す。

### 3.4 GIC（PL390 = GICv1、virt の GICv2 と register は同じ）

arm64 の `gic.c` の写し（`rpi_gic_*` → `armv7_gic_*`）。違い: `ARM64_DIRECT_BASE` でなく識別の address（zybo は identity の `0xF8F01000`/`0xF8F00100`、virt は FDT の `reg` を device 窓に張る）、`dsb ish` は `dsb` の `ish` 付き（ARMv7 でも `dsb ish` は有る）、spin lock は §3.8。CPU は secure state（BootROM→U-Boot→kernel、TrustZone の切り替え無し。QEMU の zynq も secure で入る）なので Group 0 が IRQ で届く（`GICC_CTLR` = EnableGrp0|EnableGrp1、FIQEn=0、AckCtl=1）。版 3（R24）: virt は Non-secure の SVC で入り、QEMU の virt の GIC は既定で security extension を持たない（`secure=off`）ので、`GICC_CTLR`・`GICD_CTLR` の Enable の bit だけを書く。group の register は zybo（Secure）でだけ書く（bsp の `bsp_gic_bases` と同じ所の flag）。`hal_irq_set_affinity` は `ITARGETSR` で CPU1 に振れる（既定は全部 CPU0、ws208 の方針と同じ）。MSI は `HAL_ERR_UNSUPPORTED`。

### 3.5 TLB と ASID

- ASID 8 bit（CONTEXTIDR[7:0]）。space ごとに ASID と世代を持ち、255 個を使い切ったら世代を上げて `TLBIALLIS`。
- space の切り替え（`hal_space_switch`）は ARMv7 の決まった順に、版 3（M8）で erratum 754322 の `dsb` を足す: **`dsb`**（754322）→ CONTEXTIDR に予約の ASID 0 → `isb` → TTBR0 → `isb` → **`dsb`**（754322）→ CONTEXTIDR に新しい ASID → `isb`。754322 は Cortex-A9 r2p*・r3p*（Zynq は r3p0）の「A speculative memory access may cause a page table walk which starts prior to an ASID switch but completes afterwards … places two dsb instructions in the mm switching code」（Linux v6.12 `arch/arm/Kconfig`、`proc-v7-2level.S` 53〜55 行の `#ifdef CONFIG_ARM_ERRATA_754322` の `dsb`。Procedure referenced）。QEMU は erratum を再現しないので、確かめは実機だけ（p005 以降の実機の受け入れが通る事。直接の試験は無い）。
- A9 MPCore は `ACTLR.SMP=1`・`ACTLR.FW=1` で TLB・cache の maintenance を他の CPU に broadcast する → `hal_space_flush_tlb(_range)` は `TLBIMVAIS`（ASID 付き）/`TLBIASIDIS`、kernel は `TLBIMVAAIS`、その後 `dsb; isb`。IPI の shootdown は要らない（virt の A15 も同じ）。
- 版 3（H3）: `ACTLR.SMP|FW` と SCU の有効化は **WS286 の 1 CPU の boot で行う**（§3.2）。後の SMP の WS で足す erratum: 764369（2 CPU 以上の A9 の全版、MVA の cache maintenance の前の `dsb` と SCU の diagnostic の bit）。751472 は r3p0 より前、742230・743622 は r1p0〜r2p2・r2p*（Linux の Kconfig）なので Zynq の r3p0 には要らない。

### 3.6 timer（BSP の ops）

`struct armv7_clock_ops { void (*init_boot)(void); void (*init_cpu)(void); bool (*read_counter)(uint64_t *c, uint64_t *hz); void (*ack_tick)(void); uint32_t tick_irq; }`。

- zybo: tick は **private timer**（`0xF8F00600`、PPI 29、periodic、load = periphclk/1000 − 1、prescaler 0）、`hal_rtc_read_counter` は **global timer**（`0xF8F00200`、64 bit、上→下→上の読み直し、prescaler 0）。periphclk = CPU/2 を SLCR（ARM PLL の FDIV × PS_CLK / ARM_CLK_CTRL の DIVISOR / 2）から計算（p002 の実測の SPL の後の値: FDIV 26、DIVISOR 2 → CPU 650 MHz、periphclk 325 MHz、§4.8）。QEMU と判定した時（§1.4 の `PSS_IDCODE` = 0、版 3）は 100 MHz（QEMU の SLCR の reset 値で計算すると 162.5 MHz になり誤る: review H1）。速さは QEMU zynq と実機の両方で、`TIMER 10S` の行の到着を host の時計で測って確かめる（§7.3・§7.4）。global timer の 64 bit は boot で 0 に揃えて走らせ、両 CPU で同じ値（1 つの counter）。
- virt: tick は generic timer の CNTP（`CNTP_TVAL`、PPI 30、FDT `arm,armv7-timer`）、counter は `CNTPCT`（`CNTFRQ` = 62.5 MHz）。
- `HAL_TIMER_FREQUENCY` = 1000（`include/hal/arch/armv7.h`）。`hal_rtc_read_epoch_time` は false（RTC 無し。時刻は networkd の NTP か `date` で）。
- 版 3（M3）: `hal_cpu_idle` は hal.h の契約（「Atomically enable interrupts and halt, then return with IRQs disabled.」194〜197 行）どおり、**IRQ を mask したまま** `dsb; wfi`（ARMv7 の `wfi` は CPSR.I が立っていても pending の割り込みで起きる）→ `cpsie i; isb`（ここで pending の IRQ が入る）→ `cpsid i` で戻る。版 2 の「IRQ を enable して wfi」は `cpsie i` と `wfi` の間の割り込みで次の tick まで眠る競合が有った。`hal_cpu_idle_suspend` は `idle-suspend-unsupported.c`。

### 3.7 SMP（CPU1）

**版 2: WS286 では作らない**（到達目標に要らない）。WS286 の HAL は `hal_cpu_count()` = 1、`hal_cpu_start_others()` は何もしない、spin lock・per-CPU・IPI の形は下のまま（後の WS で CPU1 を足す時に作り直さない）。QEMU は `-smp 1` で走らせる（zynq の ELF boot で 2 つの CPU が入口に来る問題を避ける）。`locore.S` の MPIDR の park は入れておく（`-smp 2` を誤って付けた時の保険、数行）。下は後の WS の設計。

- `locore.S` の `_start`: `cpsid if`、SVC mode、MPIDR の Aff0 ≠ 0 なら **park**（§1.3: QEMU の ELF boot と、実機の fallback の両方で使う）: `wfe` → release word（`.data` の `armv7_secondary_release[cpu]`、uncached に読むため MMU off の間は物理で）を読み、0 でなければ飛ぶ。
- CPU0 の `hal_cpu_start_others()`（`smp.c`、BSP の `bsp_cpu_release(cpu, entry_phys)`）:
  - zybo: (1) OCM `0xFFFFFFF0` に `armv7_secondary_entry` の物理を書き（Strongly-ordered の alias で）、`dsb; sev`。(2) 同時に release word も書く（QEMU の park loop 用）。(3) 100 ms 待って `armv7_secondary_boot[cpu].online` が立たなければ Linux の方法（§1.3 の 2: SLCR unlock、`A9_CPU_RST_CTRL` の bit1|bit5 を立てる、物理 0 に trampoline、clean、bit を落とす）をもう一度試し、それでも駄目なら CPU1 無しで続ける（`hal_cpu_count()` = 1、画面に `ARMV7 CPU 1 ABSENT`）。
  - virt: PSCI `CPU_ON`（function `0x84000003`、MPIDR、entry、context）。版 3（M2）: conduit は DTB の `/psci` の `method` で `hvc #0` か `smc #0`（既定の virt は HVC、§1.3）。`SYSTEM_OFF` `0x84000008`・`SYSTEM_RESET` `0x84000009` も同じ conduit。
- 二次 CPU の入口 `armv7_secondary_entry`: MMU off で SCTLR・ACTLR.SMP・TTBR・TTBCR・DACR を CPU0 と同じに、MMU on、per-CPU の stack と TPIDRPRW、GIC の CPU interface（`armv7_gic_init_cpu`）、timer の `init_cpu`、`hal_task_create_for_init_context()`、`kernel_secondary_entry(cpu)`。arm64 の `arm64_secondary_main` と同じ順。
- IPI: SGI 0 = notify（`kernel_cpu_notify_handler`）、SGI 1 = stop（`hal_cpu_park`・`hal_cpu_panic_all`）。`hal_task_transfer` は arm64 と同じ。
- SCU: 版 3 で有効化は WS286 の boot に移した（§3.2、p002 の実測では SPL の後も無効）。SMP の WS では `SCU_INVALIDATE_ALL`（+0x0C）で CPU1 の tag を invalidate してから起こす。CPU1 の BootROM の loop は p002 で実機の code を読んで確かめた（`0xFFFFFFF0` が loop 自身の `0xFFFFFF2C` でなくなるまで `wfe`、§4.8）。

### 3.8 atomic・barrier・spin

- `HAL_ATOMIC_STYLE_NATIVE`: ARMv7-A の `ldrex/strex`、64 bit は `ldrexd/strexd`（A9 は持つ。clang は `-march=armv7-a` で 64 bit の `__atomic` を inline にする見込み。**p004 で `llvm-nm -u` に `__atomic_*`・`__sync_*` が出ない事を確かめ、出たら `HAL_ATOMIC_STYLE_ENTER` に切り替える**（i386・riscv32 の形））。
- `hal_atomic_uint_try_acquire`: `ldrex/strex` + `dmb`。`hal_atomic_relax`: `yield`。
- `hal_mb` = `dmb ish`、`hal_rmb`/`hal_wmb` = `dmb ish`（ARMv7 に ld/st の片側は無い）、`hal_io_*mb` = `dsb`（device の順序は `dsb`）。
- spin lock（`spin.h`）: arm64 の `arm64_spin_lock` の写し（IRQ を止めて取る、`wfe`/`sev` は使わず `yield`）。

### 3.9 user/kernel の ABI

- AAPCS、**EABI hard-float**（`armv7-unknown-zedbsd-eabihf`）、ARM mode だけ（Thumb-2 は作らない: user の code は `-marm`。user が Thumb に入っても CPSR.T を frame の spsr で保つので動くが、signal の handler の入口は ARM とみなす（`spsr.T = 0`）。Thumb の binary の支援は Future Work）。
- syscall: `svc #0`、r7 = 番号、r0–r5 = 引数、r0 = 戻り（`intptr_t` 32 bit。64 bit を返す syscall は i386 と同じ扱い）。
- TLS: `TPIDRURO`（user が `mrc p15, 0, r0, c13, c0, 3` で読む。`-mtp=cp15`）。`hal_task_set_tls` は task の値を保ち、switch で書く。`TPIDRURW` は user に自由（保存・復元する）。`TPIDRPRW` は kernel の per-CPU。
- stack: 8 byte 揃え（AAPCS の public interface）。`HAL_TASK_SIGNAL_FRAME_ALIGNMENT` は 16（riscv32 と同じ、強い方）。
- signal frame（`include/hal/arch/armv7.h`、riscv32 と同じ head 16 byte: token・reserved・context_pointer・padding。`HAS_RESTORER`=0、`HAS_SIGNO`=0、`HAS_INFO_POINTER`=0）: `hal_task_signal_enter` は frame を task の `signal_frame[depth]`（版 3: 20 word の `struct armv7_frame`、§3.3）と VFP（`signal_fpregs`、d0–d31 + FPSCR）に保ち、r0 = signo、r1 = siginfo、r2 = ucontext、**lr = restorer**、sp = stack、pc = handler、spsr = user・ARM・IRQ enable。`hal_task_signal_return(token)` は token が一致する深さの frame を戻す（arm64 の `task.c` 416〜 と同じ形）。
- `struct hal_gpregs { uint32_t r[13]; uint32_t sp; uint32_t lr; uint32_t pc; uint32_t cpsr; uint32_t tpidruro; }`、`struct hal_fpregs { uint64_t d[32]; uint32_t fpscr; uint32_t fpexc; }`、`struct hal_vregs { uint32_t reserved; }`（NEON の register は d0–d31 と同じ物なので vregs は空）。`HAL_DEBUG_HAS_SINGLE_STEP` = 0（ARMv7 に hardware の single step は無い。Linux の arm32 も無い）、`HAL_DEBUG_POINT_MAX` = 1 で全部 0（A9 の DBGBCR/DBGWCR の利用は後の Phase）。
- `hal_task_exec_validate`: entry が user の範囲で 4 byte 揃え（ARM）、stack が 8 byte 揃え。

### 3.10 VFP/NEON の lazy な切り替え

- boot で CPACR の cp10/cp11 を full access。zybo（Secure）は NSACR が要らない。virt（Non-secure で入る）は QEMU の firmware の真似が NSACR の cp10/cp11 を許す見込み（推測）で、`VFP PASS`（§7.6）が確かめる。`FPEXC.EN` は **0 で始め**、user が VFP/NEON の命令を使うと Undefined → `fpu.c` が「この CPU の owner」を見て、別の task なら前の owner の d0–d31・FPSCR を保存し、今の task の物を復元、`FPEXC.EN=1` で戻る。switch では `FPEXC.EN` を落とすだけ（owner は per-CPU）。`hal_task_transfer` で CPU を跨ぐ時は元の CPU で保存する（arm64 の watch と同じ扱い。mips の設計の lazy と同じ）。
- 版 3（M6）**sync と forget**（`src/hal/mips/fpu.c` の `mips_fpu_sync`（201 行〜: owner なら hardware から保存）・`mips_fpu_forget`（228 行〜: owner を捨てる）と、`mips/task.c` の呼び出しの所に合わせる）:

| 時 | 操作 | mips の対応 |
| --- | --- | --- |
| fork（`hal_task_create` の子の写し） | 親を sync してから `fpregs` を写す | task.c 318 行 |
| exec（`hal_task_exec` の space の差し替え） | 初期値の `fpregs` にして forget | 389 行 |
| signal の入口 | sync してから `signal_fpregs[depth]` に写す | 485 行 |
| signal の戻り | `signal_fpregs[depth]` を戻して forget | 536 行 |
| task の破棄 | forget（per-CPU の owner が解放済みの task を指さない） | 565 行 |
| debug の読み（`hal_task_get_fpregs`）・書き | 読みは sync、書きは写してから forget | debug.c 125・161 行 |

- 版 3（M6）**Undefined の扱いを単純にする**: user から来た Undefined で FPEXC.EN=0 なら、命令を decode せずに owner を切り替えて EN=1 にし、**同じ命令をやり直す**（戻り先は ARM なら `lr_und - 4`、spsr.T が立っていれば `lr_und - 2`）。本当の未定義の命令ならやり直しで再び Undefined になり、その時は EN=1 なので `ILLEGAL_INSN`（SIGILL）。NEON の符号（`0xF2xxxxxx`〜`0xF4xxxxxx`）や Thumb の VFP の符号を表で漏らす問題が無い。EN=1 で FPEXC.EX か DEX が立っていれば（A9 の VFPv3 は support code の要らない実装と推測し、来ない見込み）FPEXC を消して `FP_EXCEPTION`（SIGFPE）。
- FPSCR の消毒: user が書く値（signal の戻り、debug の書き）は例外の cumulative bit（FPSCR[4:0]）と trap enable（[12:8]、A9 の VFPv3 は trap を持たない = 書いても 0）を整えてから持つ。
- 試験（版 3、M6 の (e): sh・login・ls は浮動小数点を使わず lazy の道を通らない）: (1) HAL の self-test `VFP PASS`（§7.6、mips の `selftest.c` の `check_float`（668 行〜）と同じ形: 2 つの task の状態に別の d0〜d31・FPSCR を置き、switch・sync・forget を通して値が保たれる事）、(2) p007 の user の試験の program `abitest`（§7.2、2 つの process が別の値を置いて互いに switch しても保たれる、signal の handler が VFP を使っても戻った後の値が保たれる、exec の後は初期値）。
- kernel は VFP を使わない（`-mfloat-abi=soft -mfpu=none`）ので、割り込みの中で VFP の状態は壊れない。

### 3.11 cache（`cache.c`）

- L1: `hal_dcache_clean_range` = `DCCMVAC` の loop（32 byte line、`CTR` から取る）+ `dsb`、invalidate = `DCIMVAC`、clean+invalidate = `DCCIMVAC`、icache = `ICIMVAU` + `BPIALL` + `dsb; isb`、`hal_sync_instruction_stream` = clean + icache invalidate。
- L2（PL310、zybo だけ、§1.4 で有ると判った時）: **A9 は L1 の操作を L2 に伝えない**ので、`hal_dcache_*` は L1 の後に **PL310 の物理 address の操作**（`CLEAN_PA` +0x7B0、`INV_PA` +0x770、`CLEAN_INV_PA` +0x7F0、各 32 byte、最後に `CACHE_SYNC` +0x730）を行う。仮想→物理は direct map（引く）か、device 窓は page table を引く（`hal_dcache_*` の引数は direct map か uncached の alias で来る前提。uncached の alias は L2 も通らない（Normal non-cacheable は PL310 を bypass）ので何もしない）。
- PL310 の初期化（**後の WS**、実機だけ。版 1 の p006）: U-Boot の状態（有効かどうか）を `CONTROL`（+0x100）で読む。無効なら: SLCR `L2C_RAM`（`0xF8000A1C`）= `0x00020202`（AR#54190、Linux `slcr.c` の `SLCR_L2C_RAM` と U-Boot の両方が書く値）、`TAG_RAM_CONTROL`（+0x108）/`DATA_RAM_CONTROL`（+0x10C）の latency（値は Linux の zynq の dts `arm,tag-latency = <2 2 2>`・`arm,data-latency = <3 2 2>`: dtsi の L2 node、p006 で読んで引く）、`AUX_CONTROL`（+0x104）に way size・prefetch・early BRESP、`INV_WAY`（+0x77C）= 0xFF を待つ、`CONTROL` = 1。**有効にするのは p006 の最後の 1 step で、それまでの Phase は L2 off で走る**（U-Boot は zynq で L2 を off にして kernel に渡す見込み。p004 で読んで確かめる）。
- 版 3（H1）: 版 2 の「QEMU の zynq は L2 の model が無い」は誤り。QEMU にも実機にも PL310 が有り（§1.4・§4.8）、どちらも SPL・reset の後は `CONTROL` = 0（実機は p002 の実測。QEMU の `l2x0` は reset で 0 の見込み）。`bsp_l2_present` は判別に使わず、HAL は WS286 では両方で `CONTROL` を読むだけにする（下）。
- **版 2: WS286 では L2 を有効にしない**（JTAG の流れで SPL は `CONFIG_SYS_L2CACHE_OFF=y`、BootROM も L2 を off のまま渡す。HAL は `CONTROL` を読み、有効なら clean+invalidate して止める）。L2 off なら outer の操作は要らず、DMA（devcfg の PCAP）の一貫性は L1 の clean だけで足りる。PL310 の有効化と outer の操作は後の WS。
- erratum 775420 の規則（§3.3）: fault し得る address への MVA の maintenance を足す時（`cacheflush` の syscall）は、fault の fixup の道の `isb` の前に `dsb` を置く。

### 3.12 memory の管理（`hal_pmem_*`）

- RAM の範囲（版 3、C1・M9）: zybo は `0x00100000`〜`0x1FFFFFFF`（DTB の `memory@0` が有ればその大きさ、ただし先頭の 1 MiB は常に外す）。PA `0x0`〜`0xFFFFF` は RAM として報告せず、direct map にも張らず、読みも書きもしない（p002 の実測: `0x0`〜`0x2FFFF` は低位 OCM、`0x30000`〜`0xEFFFF` は external abort、`0xF0000`〜`0xFFFFF` は高位 OCM の alias で CPU1 の BootROM の loop と `0xFFFFFFF0` が見える、§4.8）。virt は FDT の `memory`（`0x40000000` から）で、QEMU の loader の居る `0x40000000`〜`0x4000FFFF` は予約。どちらも予約: kernel image（`_start`〜`_end`）、boot の page table、DTB（`r2` の FDT の `totalsize`。zybo は `r2` に magic が無ければ `0x01F00000` に magic が有る時だけそれ（QEMU zynq の ELF の道、§4.6）。読み終えたら解放）、ramdisk（DTB の `/chosen` の `linux,initrd-start`・`linux,initrd-end`。kernel の ramdisk が使う間は解放しない）。`hal_pmem_to_kernel` は §3.2。`hal_pmem_alloc_limited` の 32 bit は常に満たす。
- 版 3（M9 の比較）: kernel か jtag-boot が U-Boot 本体と同じ `OCM_CFG = 0x1F`・SCU の `FILTER_START = 0` を行って DDR を 0 から見せる案は採らない（p002 の実測で `FILTER_START` は `0x00100000`）。動いている system の memory の地図を変える事になり、高位 OCM に居る CPU1 の BootROM の loop への影響を実機で確かめる手間が要る。WS286 は 1 MiB を捨てるだけ（512 MiB の 0.2%）。SD の起動の WS で U-Boot 本体が変えた後の地図でも、同じ「先頭 1 MiB を使わない」で動く。
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
| `hal_get_arch_handoff` | `boot.command-line`（DTB の `/chosen/bootargs`、無ければ NULL）、`armv7.fdt`・`armv7.initrd`（物理の範囲 = 2 つの 64 bit の word、riscv32 の `riscv32.fdt`・`riscv32.initrd` と同じ形、initrd は無ければ NULL） | 名前付きの値は riscv32 と同じ形。版 1 の `armv7.boot-cpu` は使う側が無いので外した（WS271 の review の Minor 4 と同じ判断） |
| `hal_reset` | zybo: SLCR unlock → `REBOOT_STATUS` の上位 bit を消す → `PSS_RST_CTRL`（`0xF8000200`）= 1（Linux `zynq_slcr_system_restart`）。jumper が JTAG なら BootROM は JTAG の待ちに戻る（次は `jtag-boot.sh`）。版 3（L1）: QEMU zynq の `PSS_RST_CTRL` はただの store で reset しない（`zynq_slcr.c` 471 行）ので、書いてから 1 s 経っても走っていれば `ZYBO RESET FAILED` を出し、IRQ を止めて `wfi` の loop で止まる。virt: PSCI `SYSTEM_RESET`（DTB の method の conduit、§3.7） | host からの reset は §4.3 の DAP 経由の同じ書き込み |
| `hal_poweroff` | zybo: unsupported（USB 給電。SwitchBot のプラグ AC276E42B252 は 2026-10-23 時点で ZYBO に繋がっていない: ユーザー D2「SwitchbotはまだZYBOに接続されていません」。どの道具も ZYBO のために電源を切らない）、virt: PSCI `SYSTEM_OFF` | |
| `hal_entropy_fill` | global timer の下位 bit と XADC（後）。最初は counter だけ（弱い、`kern` 側の pool が混ぜる） | |
| `hal_get_memstat` | arm64 と同じ | |

**hal.h を変える必要は無い**。事前承認の形（WS263〜266・271 と同じ、Guardrail 2026-10-18 の行）で足す物:
1. `include/hal/arch.h` に `HAL_ARCH_ARMV7` の節（`#include <hal/arch/armv7.h>`）。
2. `include/hal/arch/armv7.h` を新規（§3.9 の値。riscv32.h と同じ節の並び）。
3. `hal_get_arch_handoff` の名前付きの値 `boot.command-line`・`armv7.fdt`・`armv7.initrd`（上の表）。
提案する差分は [proposed/hal-armv7.md](proposed/hal-armv7.md)（版 2 で書いた: D1 arch.h、D2 armv7.h の本文、D3 名前）。**U1 は 2026-10-23 にユーザーが推しで決定**（「U項目は読みましたが、全部推しでOKでした。」、Guardrail の表）。版 3 の review の対応で HAL の API に足す物は無い（影の table・例外の frame・VFP の sync/forget・SCU と ACTLR は全部 HAL の内側。C2 の直しでも `armv7.initrd` の値の出所は同じ `/chosen`）。

- **新しい syscall `cacheflush(addr, len)`**（§2.3 の `__clear_cache`: ARMv7 の user は icache を自分で操作できない）: これは kernel の syscall の表（`include/uapi/syscall.h`、今の最後は `KERN_SYS_sandbox_spawn = 170`）の変更で、HAL の API ではない。**版 2: WS286 では作らない**（到達目標の userland に JIT は無い。版 3: compiler-rt の `clear_cache.c` は armv7 の manifest に入れない。`__clear_cache` が未定義で link が落ちる program が出たら、その時に `cacheflush` の WS を前に出す）。JIT（Noct、browser）を armv7 で動かす WS で Q1 に番号を頼む。

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
int  bsp_is_emulated(void);              /* 版 3: zybo は PSS_IDCODE == 0（§1.4）、virt は常に 1 */
void bsp_cpu_early_setup(void);          /* 版 3: MMU on の前。zybo は SCU と ACTLR.SMP|FW（§3.2）、virt は空 */
```

版 3: 版 2 の `bsp_l2_present` は外した（QEMU も実機も PL310 を持ち、WS286 は `CONTROL` を読むだけ、§3.11）。`locore-board.h`（arm64 と同じ）: 板ごとの `armv7_board_core_index`（MPIDR → 番号）と `ARMV7_PHYS_BASE`・`ARMV7_KERNEL_PHYS`（§2.2）。image は link の場所（LMA）に置かれて来るので置き直しは無い（zybo の jtag-boot の raw・ELF、QEMU zynq の ELF、virt の raw の全部）。

## 4. 起動の道（版 2: JTAG が先、SD の起動は後の WS。版 3 で §4.1・§4.3・§4.6 を直した）

### 4.1 全体と選ぶ物

開発の起動は **JTAG**（boot mode の jumper は JTAG のまま）。host の OpenOCD（ZYBO の板の FT2232H の A channel）で次を 1 回の script で行う:

1. **reset**: DAP の AHB-AP から SLCR を unlock して `PSS_RST_CTRL` = 1（§4.3）。BootROM は JTAG mode で何も読まず、CPU0 を待たせる。
2. **halt**: CPU0 を BootROM の待ちで止める。SLCR `BOOT_MODE` [2:0] = 0（JTAG）を読んで確かめ、違えば止める（jumper が SD の時に DDR を壊さない）。
3. **SPL**: U-Boot v2025.07 の SPL（`spl/u-boot-spl.bin`、GPL-2.0+、ZYBO の `ps7_init_gpl.c` を含む）を OCM の `0x00000000` に load して `0x0` から走らせる。SPL が clock・MIO・DDR を初期化し、UART1 に banner を出す。
4. **止める**: SPL の `board_boot_order` の入口の hardware breakpoint で止まる（§4.2 の理由: その先で SPL は RAM の中身を image と見て飛ぶ）。breakpoint を消す。
5. **load**: kernel `vmunix.bin`（`llvm-objcopy -O binary`）を DDR `0x00100000`、DTB を `0x01F00000`、root の ramdisk（`root.img`、任意）を `0x04000000` に load し、`verify_image` で読み戻す。
6. **入る**: `r0` = 0、`r1` = 0xFFFFFFFF（machine id は使わない）、`r2` = DTB の物理、`cpsr` = 0x1D3（SVC、IRQ・FIQ・abort を mask、ARM）、I-cache と分岐予測を invalidate（`arm mcr 15 0 7 5 0 0`・`arm mcr 15 0 7 5 6 0`）、`resume <入口>`、OpenOCD を `shutdown` で終える（target は走ったまま）。版 3（C2）: **入口の register の約束は QEMU virt の Linux の loader（§1.3、`r0 = 0`・`r1` = board id・`r2` = DTB）と同じ**で、後の U-Boot の `bootm` とも同じ。HAL は `r1` を見ない。
   - 版 3（L5、p002 で実装済み: §4.8）: 入口は決め打ちでなく、raw なら load の address（= `ARMV7_KERNEL_PHYS` = `0x00100000`）、ELF なら `e_entry`。kernel の範囲が `0x00100000`〜DTB（`0x01F00000`）に、initrd が `0x1FC00000` の下に収まる事を load の前に確かめる。kernel の `_end`（BSS を含む）が DTB に届かない事は ELF の時だけ確かめられるので、p005 の jtag-boot の直しで raw の時も同じ名前の ELF の `_end` を読む。`ps7_init()` の戻り値は SPL が見ない（U-Boot `mach-zynq/spl.c` 25〜31 行）ので、DDR の照合（p002 の 8 MiB）は p005 以降も 1 回目の起動の前に残す。
   - 版 3（L4）: OpenOCD を timeout で kill すると DBGDSCR の halting debug の bit が残り、kernel・user の `bkpt` が CPU の停止になり得る（推測、p002 で未試験）。道具は `shutdown` で終え、kill した時は次に `jtag-reset.sh` を挟む（p002 の運用、§4.8）。
7. console は UART1 = `/dev/serial/by-id/usb-Digilent_Digilent_Adept_USB_Device_210279785182-if01-port0`（今の `/dev/ttyUSB1`、115200 8N1）。

| 案 | 中身 | 判断 |
| --- | --- | --- |
| A（採る） | OpenOCD + **U-Boot SPL** で DDR を初期化し、kernel を JTAG で DDR に置いて飛ぶ | OSS だけ（§12 の決定）。`ps7_init_gpl.c` は U-Boot に入っている ZYBO の物（Vivado の export を Digilent の設定で作った物、GPL-2.0+）で、silicon の版の選択（`ps7_init` の v1/v2/v3）も SPL がする。同じ SPL を後の SD の起動の BOOT.BIN に使える。host に `gcc-arm-linux-gnueabihf` 14.2 が既に有る |
| B（fallback） | `ps7_init_gpl.c` の表（`EMIT_MASKWRITE`・`MASKPOLL`・`MASKDELAY` の列）を OpenOCD の `mww`/poll の Tcl に変換して流す。SPL を走らせない | U-Boot の build が要らない。ただし変換の tool と poll・delay・版の選択の意味を自分で作り、生成物は GPL のデータの派生（tree に入れない）。SPL の build か実行が落ちた時の案 |
| C | SPL に FIT の kernel を渡して SPL に起動させる（`SPL_LOAD_FIT_ADDRESS` = 0x10000000、`SPL_OS_BOOT`） | `ps7_post_config` も走る利点は有るが、FIT を作る `mkimage` が要り、SPL の image の解析に依存する。採らない |
| D | Xilinx の xsdb/Vivado の `ps7_init.tcl` | AMD の tool（OSS でない）。§12 の決定で採らない（難しい時にユーザーが入れる） |
| E | 版 1 の SD + U-Boot の起動を先に | jumper の付け替えと SD の書き換えが毎回要る。JTAG の方が速く回る（§12「JTAGが簡単に利用できるなら、SDブートよりも先に」）。後の WS |

### 4.2 SPL の JTAG での振る舞い（U-Boot v2025.07 を読んで確かめた事）

- `CONFIG_SPL_TEXT_BASE` の zynq の既定は `0x0`（`common/spl/Kconfig` の `default 0x0`、zynqmp だけ `0xfffc0000`）。`CONFIG_SPL_MAX_SIZE=0x30000`（defconfig）= 192 KiB、`u-boot-spl.lds` の `.sram` は `IMAGE_TEXT_BASE` から `IMAGE_MAX_SIZE`。BootROM の後の OCM は低位 192 KiB（`0x0`〜`0x2FFFF`）と高位 64 KiB（`0xFFFF0000`〜）に居る前提（UG585 は未読。SPL の配置がこの前提で作られている事は確か。**p002 の probe で `0x0` と `0xFFFF0000` の書き読みと `OCM_CFG`（`0xF8000910`）の値を記録する**）。
- SPL の stack は `CONFIG_SPL_STACK=0xfffffe00`（高位 OCM、下に伸びる）。BSS は DDR の `0x00100000`〜`0x001FFFFF`（`SPL_BSS_START_ADDR`・`SPL_BSS_MAX_SIZE`、`SPL_SEPARATE_BSS` は `ARCH_ZYNQ` の select）、`SPL_STACK_R_ADDR=0x200000`。**kernel の load は SPL を止めた後なので、この範囲を上書きしてよい**。
- `board_init_f`（`mach-zynq/spl.c`）→ `ps7_init()` → `arch_cpu_init()`。SPL の build（`CONFIG_XPL_BUILD`）の `arch_cpu_init` は SLCR の unlock と lock だけで、**`OCM_CFG` = 0x1F（OCM を全部高位へ）と SCU の `filter_start` = 0（DDR を 0 から）は U-Boot 本体だけ**（`cpu.c` の `#ifndef CONFIG_XPL_BUILD`）。→ JTAG の流れの kernel は OCM が低位に残った状態で入る（§3.12 の予約で扱う）。
- `board_init_r`（`common/spl/spl.c`）→ `spl_soc_init()`（`ARCH_ZYNQ` が `SPL_SOC_INIT` を select）→ `preloader_console_init()`（UART1 に `U-Boot SPL 2025.07 …`）→ `board_boot_order()` → `spl_boot_device()`: `ZYNQ_BM_JTAG`（0）→ **`BOOT_DEVICE_RAM`**。`SPL_RAM_DEVICE` は `ARCH_ZYNQ` で既定 y → `spl_ram_load_image`: `SPL_LOAD_FIT_ADDRESS`（0x10000000）に FIT の magic が無ければ legacy の header を `CONFIG_TEXT_BASE` − 64 に探し、header が無い時の扱い（raw の image と見て `CONFIG_TEXT_BASE` = 0x4000000 へ飛ぶか、失敗して `hang()`）は config 次第。**どちらでも DDR の不定の中身を実行し得るので、`board_boot_order` の入口で止める**（版 1 の §12 の「未確認」はこれで解消: JTAG mode の SPL は RAM から起動しようとする）。
- `ps7_post_config()`（PL の level shifter の有効化、PL の reset の解除）は jump の直前の `spl_board_prepare_for_boot()` だけが呼ぶ → **この流れでは走らない**。PL を使う前に zedBSD（devcfg の driver、§5.6）が `LVL_SHFTR_EN` と `FPGA_RST_CTRL` を設定する。
- defconfig は `CONFIG_SPL_SYS_DCACHE_OFF=y`・`CONFIG_SYS_L2CACHE_OFF=y`: SPL は D-cache と L2 を使わないので、JTAG の memory の書き込みの後に cache の clean は要らない。I-cache は SPL の `start.S` が有効にし得るので手順 6 で invalidate する（kernel の `locore.S` も最初に `ICIALLU`・`BPIALL` をする）。
- load する物は **`spl/u-boot-spl.bin`**（`OF_CONTROL` の DTB が後ろに付く）。ELF の `spl/u-boot-spl` は DTB を持たないので load しない。breakpoint の address は ELF の symbol（`llvm-nm` か `arm-linux-gnueabihf-nm` で `board_boot_order`）から取る。SPL は LTO 無し（defconfig に `CONFIG_LTO` 無し）なので symbol は残る。
- SPL の build: tree の外の GPL の build（§4.4 の `build-spl.sh`）。zedBSD の toolchain（共有の LLVM）は使わない・変えない。

### 4.3 OpenOCD の設定 `plan/tools/zybo/openocd-zybo.cfg`（新規、Zlib）

- interface: install 済みの `source [find interface/ftdi/digilent-hs1.cfg]`（FT2232H `0403:6010`、`ftdi device_desc "Digilent Adept USB Device"`、channel 0、TCK/TDI/TDO/TMS だけ、`reset_config none` = ZYBO の板の JTAG と同じ形。file は写さず install の物を読む）と `ftdi serial 210279785182`（この板に固定）。`adapter speed` は `$::env(JTAG_KHZ)`、既定 10000 kHz（落ちたら 1000、upstream の `zynq_7000.cfg` の値）。
- 版 3（L4、p002 の実装）: interface の板の固定は OpenOCD 0.12 の今の名前 `adapter serial 210279785182`（`ftdi serial` は旧名）、`adapter speed` の既定は 15000 kHz（p002 の実測、§4.8）。
- target: **upstream の `target/zynq_7000.cfg` を source しない**。版 3 で訂正（p002 の実測）: 版 2 の「この板の IDCODE が upstream の列に無い」は誤りで、実際の値は `0x13722093`（version 1。openFPGALoader は version を落として `0x03722093` と出す）で upstream の列に有る。自分の cfg にする理由は **`target smp` を避ける事だけ**（下）。cfg は `-ignore-version` で両方を受ける。自分の宣言（値は upstream から、Values referenced: OpenOCD 0.12.0 `tcl/target/zynq_7000.cfg`）:
  - `jtag newtap zynq_pl bs -irlen 6 -ircapture 0x1 -irmask 0x03 -expected-id 0x03722093 -ignore-version`
  - `jtag newtap zynq cpu -irlen 4 -ircapture 0x1 -irmask 0xf -expected-id 0x4ba00477`
  - `dap create zynq.dap -chain-position zynq.cpu`
  - `target create zynq.cpu0 cortex_a -dap zynq.dap -coreid 0 -dbgbase 0x80090000`
  - `target create zynq.cpu1 cortex_a -dap zynq.dap -coreid 1 -dbgbase 0x80092000 -defer-examine`（**`target smp` にしない**: CPU1 は BootROM の待ちのまま触らない。probe の時だけ `arp_examine` して読む）
  - `target create zynq.ahb mem_ap -dap zynq.dap -ap-num 0`（CPU を止めずに system の bus を読み書きする口。Zynq の DAP の AP0 が AHB-AP である事は **未確認**: p002 の probe で `zynq.dap info 0`・`info 1` と `zynq.ahb mdw 0xF8000530`（`PSS_IDCODE`）を記録する）
  - TAP の宣言の順は upstream と同じ。p002 の最初に `scan_chain` で 2 つの TAP と IDCODE を確かめる（確かめた: §4.8。AP0 は AHB-AP）。
  - 版 3（L4）: p003 の GP0 の block の読み書きは、止めた cortex_a の CPU（`mdw phys`）でなく `zynq.ahb`（AHB-AP）で行う。GP0 が応答しない時に CPU が debug の中で固まるのを避け、AHB-AP の error（sticky）なら DP の ABORT で戻せる見込み（推測。p002 の実測では DDRC の前の DDR の読みで AHB-AP が sticky error で固まった: §4.8。戻し方は p003 で確かめる）。p003 の順は Linux の手順に合わせて「`pl-jtag.sh` で書く → `zybo_pl_enable`（level shifter `0xF`・`FPGA_RST_CTRL` = 0）→ 読み書き」。
- proc（同じ file）: `zybo_ps_reset`（`zynq.ahb mww 0xF8000008 0xDF0D`、`zynq.ahb mww 0xF8000200 1`。reset で DAP の応答が切れても成功とみなす: 2 つ目の write の error は `catch`）、`zybo_ps_reset_via_cpu`（AHB-AP で届かない時: cpu0 を halt して `mww phys` で同じ 2 つ）、`zybo_check_jtag_mode`（`BOOT_MODE` を読む）、`zybo_spl SPL_BIN BP_ADDR`、`zybo_load ADDR FILE`（`load_image FILE ADDR bin` + `verify_image FILE ADDR bin`、経過時間から速さを出す）、`zybo_enter ENTRY DTB`、`zybo_pl_enable`（SLCR unlock、`LVL_SHFTR_EN`（`0xF8000900`）= 0xF、`FPGA_RST_CTRL`（`0xF8000240`）= 0、lock。p003 の zedBSD 無しの PL の試験用）。
- USB の権限: `/dev/ttyUSB0` が group plugdev（openocd の udev の規則）で、awe は plugdev に居るので sudo 無しで開ける見込み。libusb の権限で落ちたら `sudo`（AGENTS.md で許される）で走らせ、どちらで動いたかを記録する。
- OpenOCD は interface 0 から `ftdi_sio` を外す（使う間 `/dev/ttyUSB0` が消える）。interface 1（UART）は残るが、番号が変わり得るので **UART は必ず by-id の path で開く**。

### 4.4 道具（p002 で書く。Q1 が Master の Tools に登録する）

版 3: 下の表は版 2 の計画。p002 で実際に作った道具と名前（`spl-build.sh`、`jtag-probe.sh`、`uart-expect.py`、`zybo-common.sh`、`adapter serial`）は §4.8 と [phase002](phase002/phase.md) が正。`pl-jtag.sh`・`pl-build.sh` は p003、`qemu-zynq.sh` は p005 で作る。

| 道具 | 中身 |
| --- | --- |
| `plan/tools/zybo/openocd-zybo.cfg` | §4.3 |
| `plan/tools/zybo/jtag-reset.sh [--halt]` | `openocd -f openocd-zybo.cfg -c 'init; zybo_ps_reset; shutdown'`、1 s 待つ。`--halt` は別の openocd の process で `init; halt`（CPU0 が BootROM の待ちに居る事、pc を表示）。reset と次の操作を **別の process** にする: system の reset で DAP の電源の要求（CDBGPWRUPREQ）が落ちても、新しい process の `init` が立て直す |
| `plan/tools/zybo/jtag-boot.sh KERNEL.bin [--dtb FILE] [--append TEXT] [--initrd root.img] [--no-reset] [--spl-only] [--expect REGEX]…` | §4.1 の 1〜7。DTB を渡さない時は script の中の DTS の雛形（Zlib、自分の物: `memory@0` = 512 MiB、`/chosen` の `bootargs` = `--append`、`--initrd` の時 `linux,initrd-start`・`-end`）を `dtc` で自分の BUILD に作る（QEMU virt の DTB と同じ property なので HAL の読み方は 1 つ）。`--expect` は resume の前に `serial.py --device <UART の by-id> --baud 115200 expect REGEX` を裏で始め、その結果で exit する（§7.4、U3）。`--spl-only` は手順 4 で止めたまま終える（p003 の PL の試験、DDR の試験）。SPL の path は `ZYBO_SPL`（既定 `build/zybo-uboot/spl/u-boot-spl.bin` と同じ dir の ELF）。load した file の SHA-256 と速さを表示 |
| `plan/tools/zybo/build-spl.sh [BUILD]` | U-Boot v2025.07 の tarball（`https://ftp.denx.de/pub/u-boot/u-boot-2025.07.tar.bz2`）を取り、SHA-256 を script に固定（p002 で最初に取った時の値と、`raw.githubusercontent.com` の tag の file との一致で確かめる）、BUILD（既定 `build/zybo-uboot`）に展開、`make xilinx_zynq_virt_defconfig`、`make DEVICE_TREE=zynq-zybo CROSS_COMPILE=arm-linux-gnueabihf- -j4 spl/u-boot-spl.bin`。足りない host の package（bison・flex・libssl-dev など）は build が言う物を `sudo apt-get install` し記録する。SPL の binary は build/ に置くだけで配布しない（配布は SD の起動の WS で zedBSD-firmware に source の所在と `Licenses/gpl-2.0.txt` を添えて） |
| `plan/tools/zybo/pl-jtag.sh FILE.bit` | `openFPGALoader -c digilent --ftdi-serial 210279785182 --index-chain 1 FILE.bit`（option の名前は p003 で `--help` から確定。index は openFPGALoader の `--detect` の順で xc7z010 が 1）。PS が走っていても PL だけを書く。後で zedBSD 側で `fpgaload --enable` が要る（§4.2 の `ps7_post_config` が走らないため） |
| `plan/tools/zybo/pl-build.sh RTL_DIR [BUILD]` | openXC7（yosys + nextpnr-xilinx + prjxray の fasm2frames + xc7frames2bit）で `xc7z010clg400-1` の bitstream を作る（§5.10）。zedBSD の make の外 |
| `plan/tools/zybo/qemu-zynq.sh KERNEL [--initrd root.img] [--append TEXT]` | §4.6 の QEMU zynq（T1 が流す） |

- 板は 1 つなので、openocd・openFPGALoader を使う道具は全部 **host の 1 つの lock**（`flock /tmp/zedbsd-zybo-board.lock`、file は消さない）を取る。T1 と担当が同時に板に触れない。
- 速さの見積もり（未確認、p002 で測る）: OpenOCD の cortex_a の memory 書き込みは 10 MHz の JTAG で数百 KB/s。kernel 2〜4 MB は 5〜20 s、最小の ramdisk（数 MB）も同じ程度。遅すぎる時は `zynq.ahb`（AHB-AP）の `load_image` を試す。

### 4.5 PL の書き込みの道（2 つ）

- **JTAG**（`pl-jtag.sh`、openFPGALoader 0.13.1 の `--list-fpga` に `0x03722093 xilinx zynq xc7z010`）: zedBSD の無い所でも書ける。p003 で zedBSD より先に block を確かめる道。openocd と同じ FTDI を使うので lock を共有。
- **PS の devcfg/PCAP**（`fpgaload FILE.bit`、§5.6）: zedBSD の UART の shell から。bitstream は ramdisk の中（§10 U6）。WS286 は LAN を作らないので SSH の道は後の WS（LAN が来れば `scp design.bit zybo:` → `fpgaload` がそのまま使える）。
- bitstream は openXC7 で host が作る（§5.10）。BOOT.BIN に入れて起動時に書く道は作らない。

### 4.6 QEMU の起動

- `virt`（HAL の立ち上げ、boot-test.sh の新しい mode、[proposed/boot-test-armv7.md](proposed/boot-test-armv7.md) 版 3）: `qemu-system-arm -M virt,gic-version=2 -cpu cortex-a15 -smp 1 -m 512M -device ramfb -kernel build/armv7-virt/vmunix.bin [-initrd root.img] [-append …] -serial unix:… -qmp unix:… -display none`。版 3（C2）: **`-kernel` は raw の `vmunix.bin`**（ELF でない）。QEMU は Linux の約束で `0x40010000` に置き、`r2` に DTB、`-initrd` を読んで `/chosen` の `linux,initrd-start`/`-end` に、`-append` を `bootargs` に書く（§1.3 の根拠）。ELF の `vmunix` は gdb の symbol にだけ使う（`file vmunix` で VMA が合う）。最初の T1 の起動（p004）の前の確かめ: `-S -gdb tcp::PORT` で止め、gdb-multiarch で `hbreak *0x40010000`・`continue` の後に `p/x $r2` と `x/wx $r2`（FDT の magic `0xd00dfeed` は little endian の word で `0xedfe0dd0`）を見る（log を読まない判定、T1 の依頼に入れる）。console は HAL が ramfb に描く（riscv32 の bsp-virt の `framebuffer.c`・`fwcfg.c`・`cons.c`・`font.c` の写し。arm の virt の fw_cfg も MMIO で、selector と DMA の address は big endian、`docs/specs/fw_cfg.rst`）。
- `xilinx-zynq-a9`（zybo の BSP の早い確かめ、`qemu-zynq.sh`）: `qemu-system-arm -M xilinx-zynq-a9 -smp 1 -m 512M -kernel vmunix -serial null -serial unix:… -device loader,file=jtag.dtb,addr=0x01f00000 [-device loader,file=root.img,addr=0x04000000] -display none`（UART1 は 2 つ目の `-serial`）。DTB は `jtag-boot.sh` と同じ DTS の雛形を `qemu-zynq.sh` が `dtc` で作り（`bootargs` も `linux,initrd-start`/`-end` もその中）、`-device loader` で JTAG と同じ `0x01F00000` に置く。ELF の道の QEMU は `r2` を設定しない → **zybo の HAL は `r2` が RAM の中の FDT の magic（`0xd00dfeed`）を指す時はそれ、無ければ `0x01F00000` に magic が有る時だけそれ、どちらも無ければ DTB 無し**（固定の address と既定の command line）。
  - 版 3（L1）: `-dtb` を渡せば ELF の時も QEMU は DTB を PA 0 に置いて `-append` を書く（§1.3）が、`r2` は設定されず置き場所が JTAG と違うので使わない。
  - 版 3（C2 の zynq 側の比較）: QEMU zynq も raw の image で Linux の約束に乗せられる（`zynq_binfo.loader_start = 0`、`xilinx_zynq.c` 449 行）が、採らない。(1) raw の置き場所は `0x00010000`（`loader_start + 0x10000`）で zybo の `ARMV7_KERNEL_PHYS` = `0x00100000`（JTAG・実機）と違い、zybo の build が 2 つになる、(2) Linux の道は A9（EL2 無し、EL3 有り）を **Non-secure の SVC** で入れる（`do_cpu_reset` の `arm_emulate_firmware_reset`、`secure_board_setup` も無い）ので、Secure で入る実機の BSP（GIC の Group、ACTLR、SCU）と違う状態を試す事になる。ELF の道は cpu reset のままの Secure の SVC で実機に近い。
- QEMU は ELF の `e_entry` をそのまま pc にし、segment を `p_paddr` に置く（`hw/arm/boot.c` の `load_elf`）→ **`vmunix.ld` の `ENTRY` は物理の `_start`**（zybo: LMA `0x00100000`、VMA `0x80100000`）。JTAG は raw の `vmunix.bin` を `0x00100000` に置くか ELF を物理に置く（§4.8）ので同じ入口。virt の ELF の `e_entry`（`0x40010000`）は QEMU は使わない（raw）が、§7.1 で値を確かめる。

### 4.7 SD の起動（後の WS。版 1 の §4.1・§4.2 の要約）

- BOOT.BIN = SPL（`mkimage -T zynqimage`、同じ U-Boot の build の `spl/boot.bin`）、`u-boot.img`、FAT の `boot.scr`（`bootm` で uImage `uvmunix`、DTB は U-Boot の `zynq-zybo.dtb`）、partition 2 か FAT の `rootfs.img` の root。binary は zedBSD-firmware（GPL-2.0+、source の所在を添える）。
- jumper を SD に替えるのはユーザー。替える前に、**JTAG で SPL → U-Boot 本体（`u-boot.bin` を `CONFIG_TEXT_BASE` = 0x4000000 に load）→ `run sdboot` 相当**で boot.scr・uImage・DTB の道を jumper 無しで確かめられる。
- 再起動: `reboot`（`hal_reset`、BootROM が SD から読み直す）。止まった時は `jtag-reset.sh`（SD の mode でも同じ DAP の書き込みで reset できる）。SwitchBot は使わない（未接続）。
- kernel の更新: LAN の WS の後は `deploy.sh`（scp + `reboot`）、それまでは JTAG。

### 4.8 p002 の実測（2026-10-23 P1、実機。詳細は [phase002](phase002/phase.md)）

- 道具は §4.4 のとおり（SPL の build の script の名前は `spl-build.sh`、既定の出力 `build/zybo-spl/out/spl/`）。追加: `jtag-probe.sh`（状態を変えない probe）、`uart-expect.py`（UART の生きた line の expect、`serial.py --device` の代わり）、`zybo-common.sh`（lock）。
- SPL は `CONFIG_SPL_RAM_DEVICE` を外して build（breakpoint が外れても `hang()` で止まる）。banner `U-Boot SPL 2025.07 (Jul 07 2025 - 19:58:33 +0000)`、breakpoint `board_boot_order` = `0x00000fd8` に毎回当たる。
- reset は AHB-AP → SLCR で効く。DAP は落ちるが次の process で立て直せる（R15）。DDRC の前に AHB-AP で DDR を読むと AHB-AP が固まる。CPU0 経由の reset は DBGDSCR を DCC fast mode に残す（cfg の examine の event で直す）。
- PL の TAP の IDCODE は実際は `0x13722093`（§4.3 の「upstream の列に無い」は誤り。自分の cfg にする理由は `target smp` を避ける事だけ）。AP0 は AHB-AP（確認）。
- JTAG の load は CPU0 の DCC で約 255 KiB/s（15〜20 MHz で頭打ち、30 MHz は不可、AHB-AP は 87 KiB/s）。8 MiB ≈ 32 s。
- JTAG の流れの PA の低位: `0x0`〜`0x2FFFF` は OCM、`0x30000`〜`0xEFFFF` は external abort、`0xF0000`〜`0xFFFFF` は高位 OCM（CPU1 の BootROM の loop と `0xFFFFFFF0`）の alias。SCU の filter の開始は `0x00100000`、SCU は無効、PL310 は実機にも有り（`CACHE_ID` 0x410000c8）で無効。
- CPU1 は `0xFFFFFFF0` が `0xFFFFFF2C`（loop 自身）でなくなるまで `wfe` で待ち、変われば I-cache・分岐予測を invalidate してその値へ行く（§1.3 の方法 1 を実機の code で確認）。

## 5. BSP と driver（zybo）

版 2 の WS286 で作るのは §5.1・§5.2・§5.6・§5.7（reset だけ）・§5.9・§5.10。

### 5.1 UART（Cadence `cdns,uart-r1p8`）

- HAL の early console（`bsp-zybo/uart.c`）: 実機は SPL が設定した値を保つ（p002 の実測 `BAUDGEN` 0xAE・`BAUDDIV` 4 = 100 MHz / (174 × 5) ≈ 114943 baud、§4.8）。版 3（L1）: QEMU zynq には SPL が無く、`CR` の reset 値は `0x128`（TX_DIS・RX_DIS・STOPBRK、`cadence_uart.c` 519 行）で TX が無効だと書いても出ない（340 行）。よって **`CR` の TX_EN（bit 4）が 0 か TX_DIS（bit 5）が 1 なら**、`MR`（+0x04）= 8N1・normal、`BAUDGEN`（+0x18）= 0xAE、`BAUDDIV`（+0x34）= 4（実機の SPL と同じ値）、`CR` = TXRST|RXRST の後 TX_EN|RX_EN を書く。`FIFO`（+0x30）に書く前に `SR`（+0x2C）の TXFULL（bit 4）を待つ。bit の位置は Linux v6.12 `xilinx_uartps.c` の `CDNS_UART_CR_*`・`CDNS_UART_MR_*`（Values referenced、p005 で写す時に確かめる）。
- kernel の driver `src/drivers/serial/cdns-uart.c`（`ns16550.c` の形: `drv_cdns_uart_probe(fdt|fixed, port)`、`drv_cdns_uart_start_input(port, receive)`）: RX の割り込み（`IER` の RXTRIG（bit 0）と TOUT（bit 8）、`RXWM` = 1、`RXTOUT` = 10）で 1 byte ずつ `drv_serial_console_receive` へ。`ISR` に書いて clear。platform の binding `src/kern/platform/zybo.c` は `riscv-virt.c` の形（`drv_serial_console_init` + `probe_uart`）。
- Values referenced: Linux v6.12 `drivers/tty/serial/xilinx_uartps.c`（`CDNS_UART_*` の offset と bit）。

### 5.2 SLCR（clock・reset・MIO）

`bsp-zybo/slcr.c` + kernel の `src/drivers/platform/zybo/zynq-slcr.c`: unlock/lock（`0x008`/`0x004`、0xDF0D/0x767B）、`APER_CLK_CTRL` の bit の on/off、`GEM0_CLK_CTRL` の divisor の書き換え（速度の変更）、`FPGA_RST_CTRL`・`LVL_SHFTR_EN`（PL）、`PSS_RST_CTRL`（reset）、`A9_CPU_RST_CTRL`（CPU1）。MIO の pinmux は **変えない**（U-Boot の `ps7_init_gpl` が ZYBO の配線を済ませる。kernel は読んで確かめるだけ: UART1 の MIO48/49、SD0 の MIO40〜45、GEM0 の MIO16〜27、USB0 の MIO28〜39）。QEMU の zynq でも同じ register が有る（`zynq_slcr.c`）。

### 5.3 SD（Arasan SDHCI 8.9a）— **版 2: WS286 の外**（後の WS の設計。root は JTAG の ramdisk、§4.1）

- `src/drivers/platform/rpi4/rpi4-sdhci.c`（PIO、CMD18/25 の run、BUG-295 の回復）を **`src/drivers/mmc/sdhci.c` の共通の core**（`drv_sdhci_attach(config)`: registers、base clock Hz、quirk、割り込み番号 or poll）に切り出し、rpi4 の file は glue に、zybo の glue `zynq-sdhci.c` は base clock を SLCR の SDIO_CLK_CTRL から（50 MHz）、`SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN`（capability の base clock 0）、`CLOCK_DIV_ZERO_BROKEN`（divisor 0 を使わない）、`PRESET_VALUE_BROKEN`（Linux `sdhci-of-arasan.c` の `sdhci_arasan_pdata`）。
  - **rpi4 の file を触るのは Pi 4 の回帰が要る**。代案: zybo 用に rpi4-sdhci.c を写して `zynq-sdhci.c` にし、共通化は後（riscv32 の HAL と同じ判断）。**採るのは代案（写し）**: WS278（Pi 5）が rpi4/rpi5 の file を動かしている最中で衝突するため。共通化は Future Work。
  - 最初は PIO・poll（rpi4 と同じ）。QEMU の `sdhci.c` の model で CMD/DAT の流れを確かめてから実機。DMA（SDMA、32 bit）は速さが要る時の後の Phase。
- card detect: SD0 の CD は MIO47（SLCR `SD0_WP_CD_SEL`）。U-Boot の後なので無視して「刺さっている」前提で初期化し、失敗なら boot device 無し。

### 5.4 LAN（Cadence GEM0 + RTL8211E）— **版 2: WS286 の外**（後の WS。UART の shell で足りる）

- **`src/drivers/ethernet/cadence-gem.c` を新しい共通の core として書く**（`drv_cadence_gem_attach(const struct drv_cadence_gem_config *)`: registers、irq、descriptor の幅 32/64、DMA の制約、`set_tx_clock(speed)` の callback、MDIO の clock divisor、MAC、PHY の ops（`drv_mii_bus` + 板の PHY の初期化 callback）。中身は `rp1-ethernet.c` の GEM の部分（ring、`reclaim_transmitted`、`receive_packets`、`gem_poll`、MDIO）を 32 bit の descriptor で書き直す。**`rp1-ethernet.c` は変えない**（WS278 が動かしている。RP1 を core に乗せ替えるのは後の WS、Future Work）。dw-eqos.h と同じ config の形。
- zybo の glue `src/drivers/platform/zybo/zynq-ethernet.c`: `APER_CLK_CTRL` の GEM0、`GEM0_CLK_CTRL` の divisor を link の速さで 125/25/2.5 MHz に（Linux `macb_set_tx_clk` の rate、divisor は §1.2）、`GEM0_RCLK_CTRL`（`0xF8000138`）の enable、RGMII（GEM の `NET_CFG` の gigabit・`USRIO`? Zynq は GEM の `net_cfg` で RGMII を選ぶのでなく SLCR `GEM0_CLK_CTRL` と MIO の設定で決まる: p008 で確かめる）、MDC = pclk 108 MHz / 64 ≈ 1.7 MHz（`NET_CFG` の MDC_CLK_DIV = 0b100（div 64）; macb.h の `MACB_CLK_DIV64`）、RXUBR の割り込みで receiver を再起動（Linux `MACB_CAPS_NEEDS_RSTONUBR`: zynq の GEM の癖）。
- PHY: `src/drivers/ethernet/rtl8211e.c`（`bcm54213pe.c` の形: `drv_rtl8211e_initialize(phy, &identity)`、`read_link`）: ID `0x001cc915`、RGMII-ID の delay は extension page 0xa4 の register 0x1c の bit 13|12|11（Values referenced: Linux `realtek.c` `rtl8211e_config_init`: page select 0x1f = 7、ext page 0x1e = 0xa4、0x1c を書き、page を戻す）、autoneg は `drv_mii_phy_start_autonegotiation(bus, 0, gigabit=1)`、link は `drv_mii_phy_read_link`。ZYBO の RTL8211E の TXDLY/RXDLY の pin の strap は Digilent の回路図で未確認 → delay は register で明示する（Linux と同じ）。
- MAC: U-Boot が DTB に書いた `local-mac-address` → boot parameter `mac0=`（新しい parameter、`kernel-boot-parameters.md` に足す）→ どちらも無ければ locally administered の乱数（警告）。QEMU は `-nic user,model=cadence_gem,mac=…` を DTB 無しで渡せないので乱数で動く。

### 5.5 USB0（ChipIdea host、ULPI の USB3320）— **版 2: WS286 から外した**（2026-10-23 ユーザー、§12。LS/FS の split は Q1 の WS287）

- 事実: ChipIdea は EHCI 互換（caps = base + 0x100、ops = caps + CAPLENGTH（0x40）= base + 0x140。`USBMODE` = ops + 0x68（`ci_regs_nolpm`、zynq は lpm 無し）で CM = host（0b11）、`PORTSC` の PTS[31:30] = 0b10（ULPI）、`ULPI_VIEWPORT` = ops + 0x30）。
- 手順（p010）: (1) MIO46 の GPIO（PS GPIO `0xE000A000`、bank 1 の bit 14: DATA/DIRM/OEN）で PHY を reset（low → 10 ms → high）。(2) `APER_CLK_CTRL` の USB0。(3) `USBCMD.RST`、`USBMODE` = host|SLOM、`PORTSC.PTS` = ULPI。(4) ULPI viewport で PHY の vendor ID（0x0424 = Microchip/SMSC）を読み、OTG control に `DrvVbus|DrvVbusExternal`、`DP/DM pulldown`、function control を書く（Procedure referenced: U-Boot v2025.07 `drivers/usb/host/ehci-zynq.c` `ehci_usb_probe` と `drivers/usb/ulpi/ulpi.c`）。**ZYBO の host 口の VBUS の供給（外部電源が要るか、USB3320 の DrvVbus の配線）は Digilent の reference manual で未確認** → p010 の実機の最初の確認項目。(5) `src/drivers/pci/pci-ehci.c` の EHCI の core を `src/drivers/usb/ehci.c` に切り出し（PCI の 17 か所の呼び出しは attach/detach に局在: `drv_pci_device_map_bar/claim_bar/establish_irq/dma/set_bus_master/save・restore_enable_state`）、`drv_ehci_attach(config)`（registers、irq、dma device、ChipIdea の有無）と `pci-ehci.c` の glue、zybo の glue `zynq-usb.c`。**これは pci-ehci.c の refactor = amd64 の USB の回帰が要る**（T1 の amd64 の USB の HID/storage の試験で確かめる。切り出しは関数の移動だけ、動作は変えない）。
- 制限: 今の EHCI の driver は **high-speed の device だけ**（`pci-ehci.c` 642 行: FS/LS の port は companion に渡す。split transaction は無い）。ChipIdea は companion を持たず内蔵の TT で FS/LS を扱うので、**keyboard・mouse（LS）と FS の device は WS286 では動かない**（HS の storage・WLAN の dongle・HS hub は動く。hub の下の LS/FS も split が要るので動かない）。split transaction の実装は別 WS（Future Work、amd64 にも利く）。**U2: USB の範囲をこれで良いか。**

### 5.6 PL の書き込み（devcfg/PCAP）、PL の有効化、block の窓 — `/dev/fpga0`

- kernel: `src/drivers/platform/zybo/zynq-devcfg.c`（`drv_` の名前、platform の `zybo.c` が attach）、cdev `/dev/fpga0`（0600 root。`src/kern/devfs.c` の名前の表に `fpga` を足す = **Q1 に頼む path**）、UAPI `include/uapi/fpga.h`（新規、§5.6.1）。
- `write(2)` で bitstream を kernel の buffer（最大 4 MiB、7Z010 の全体の bitstream は約 2.08 MB。`drv_dma_alloc_coherent`（uncached、32 bit）に順に積む）に貯め、`ioctl(FPGA_PROGRAM)` で書く（`close` で書く案は、途中で落ちた時の区別が付かないので採らない）。`ioctl(FPGA_STATUS)` で `PCFG_DONE`・`INIT`・level shifter・PL の reset・最後の error を返す。sysctl `hw.fpga.done`。
- 書き込みの手順（Procedure referenced: Linux v6.12 `drivers/fpga/zynq-fpga.c` `zynq_fpga_ops_write_init`・`zynq_step_dma`・`zynq_fpga_ops_write_complete`、Values referenced: 同 file の `*_OFFSET`・`*_MASK`）:
  1. SLCR unlock、`PCAP_CLK_CTRL` enable。devcfg `UNLOCK`（+0x34）= `0x757BDF0D`（**JTAG の流れでは U-Boot 本体の `arch_cpu_init` が走らないので、kernel が必ず書く**、§4.2）。
  2. sync word（`0xAA995566`、little endian の word の列で `66 55 99 AA`）が無ければ `EINVAL`（`.bit` は userland が header を外し、word ごとに byte を反転して `.bin` の並びにする。kernel は `.bin` の並びだけ受ける）。
  3. 窓の利用を止める（§5.6.2: 書き込み中の窓の ioctl は `EBUSY`）。`FPGA_RST_CTRL`（SLCR +0x240）= 0xF、`LVL_SHFTR_EN`（+0x900）= 0 → 0xA（PS→PL）。
  4. `CTRL`（devcfg +0x00）の `PCFG_PROG_B`（bit 30）を 1 → `STATUS`（+0x14）の `PCFG_INIT`（bit 4）を待つ → 0 → `!INIT` を待つ → 1 → `INIT` を待つ（各 2.5 s の timeout、20 µs ごと。Linux 296〜343 行）。
  5. `CTRL` |= `PCAP_PR`（bit 27）| `PCAP_MODE`（bit 26）（Linux 352〜358 行の read-modify-write。暗号化の `PCAP_RATE_EN`（bit 25）は WS286 では立てない）。`STATUS` の DMA queue が空（`DMA_Q_E` bit 30 = 1、`DMA_Q_F` bit 31 = 0）でなければ `EBUSY`（363 行）。`MCTRL`（+0x80）の `PCAP_LPBK`（bit 4）を落とす（371〜372 行）。
  6. 版 3（M4）: DMA の前に **`INT_STS` に `IXR_ALL_MASK`（`0xF8F7F87F`）を書いて消す**（W1C、Linux 420 行。BootROM・前回の program・JTAG の書き込みの `D_P_DONE`・`PCFG_DONE` が残ったまま待つと、DMA の完了の前に「済んだ」と読むため）。
  7. DMA: bitstream は **1 つの物理連続の uncached の buffer**（`drv_dma_alloc_coherent`、最大 4 MiB、7Z010 の全体は約 2.08 MB）に貯め、**1 回の転送**にする（版 3、M4: 版 2 の「buffer が複数なら最後だけ | 1」は DMA queue の深さを見ずに積むと command を失う。Linux は 166〜170 行で `STATUS_DMA_Q_F` を見て止め、`DMA_DONE` の割り込み（225〜226 行）で積み足す。WS286 は 1 回なので queue を超えない）。buffer の物理は **8 byte 揃え・長さは 4 の倍数**（Linux 397〜405 行の「UG585 pg 212」の引用。driver が確かめ、違えば `EINVAL`）。`DMA_SRC_ADDR`（+0x18）= 物理 | 1（最後の転送の印）、`DMA_DST_ADDR`（+0x1C）= `0xFFFFFFFF`、`DMA_SRC_LEN`（+0x20）= word 数、`DMA_DEST_LEN`（+0x24）= 0。
  8. `INT_STS` の `D_P_DONE`（bit 12）を割り込み（devcfg IRQ 40）か poll で待つ（5 s）。error の bit（`IXR_ERROR_FLAGS_MASK` `0x00F0C860`）なら失敗。待った後にもう一度 `INT_STS` を `IXR_ALL_MASK` で消す（439 行）。
  9. 版 3（M4）: **`PCFG_DONE` は `INT_STS` の bit 2**（`IXR_PCFG_DONE_MASK`、Linux 89・496〜499 行。版 2 の `STATUS` で待つ書き方は誤りで、永久に来ない）を 2.5 s 待つ、`CTRL` の `PCAP_PR` を read-modify-write で落とす（502 行）、**PL の有効化**（次）。
- 版 3（M4）**`CTRL` の書き込みの守り**: `CTRL` には `FORCE_RST`・`JTAG_CHAIN_DIS`・`DAP_EN` など PS の reset と JTAG（この WS の唯一の起動と回復の道）を壊す bit が有る（bit の位置は UG585 未読: 推測）。driver は `CTRL` を丸ごと書かず、`zynq_devcfg_ctrl_update(set, clear)` の 1 つの helper だけが read-modify-write し、`set|clear` が許す mask（`PCFG_PROG_B`（30）・`PCAP_PR`（27）・`PCAP_MODE`（26）・`PCAP_RATE_EN`（25）、Linux の `CTRL_*_MASK` の値）の外なら `HAL_FATAL`。host の試験（`devcfg-host-test.c`）で mask の外の要求が拒まれる事を確かめる。`UNLOCK` には `0x757BDF0D` 以外を決して書かない（QEMU の model も違う値で device を閉じる: `xlnx-zynq-devcfg.c` 220〜236 行。実機の振る舞いも同じ趣旨と推測）。定数 1 つと comment と試験で固定する。
- 版 3（M4）**attach では `INT_STS` を消さない**: Linux の probe は消す（597 行）が、zedBSD では `PCFG_DONE`（INT_STS bit 2）を残し、JTAG（openFPGALoader）で PL を書いた後の `fpgaload --enable` が「DONE が立った」を読めるようにする。JTAG の書き込みで INT_STS の PCFG_DONE が立つかは **未確認**（p003 で `pl-jtag.sh` の後に AHB-AP で `0xF800700C` を読んで記録する）。立たないと分かったら `fpgaload --enable --force`（DONE の確かめを飛ばす。窓の access は人の責任と表示する）を足す。
- **PL の有効化**（`ioctl(FPGA_ENABLE)`、`FPGA_PROGRAM` の最後にも同じ物）: `PCFG_DONE` が 1 の時だけ、SLCR unlock、`LVL_SHFTR_EN` = 0xF（両方向）、`FPGA_RST_CTRL` = 0（FCLK_RESET0〜3 を解除）、lock。**SPL の `ps7_post_config` が走らない JTAG の流れ（§4.2）と、openFPGALoader で JTAG から PL を書いた後（§4.5）に要る**。`FPGA_DISABLE` は逆（窓を閉じてから reset を立て、level shifter を落とす）。
- driver の attach の時は PL に触らない（`PCFG_DONE` が既に 1 でも有効にしない: 何が書かれているか kernel は知らない。`fpgaload --enable` を人が打つ）。
- 版 3（M5）**QEMU の devcfg の model の限り**: DMA の完了で立てるのは `DMA_DONE | DMA_P_DONE` だけ（`xlnx-zynq-devcfg.c` 177〜178 行）、`STATUS` の reset 値に `PCFG_INIT` が無く（292〜294 行）、`PROG_B` に反応する code も無い → 版 2 の「`PCFG_DONE` は model が立てる」は誤りで、手順 4 の INIT の待ちは QEMU で必ず timeout する。よって QEMU zynq で確かめるのは `fpgaload --status`・UNLOCK・`CTRL` の helper・`INT_STS` の消し方・DMA の queue の確かめ・`D_P_DONE` まで。そのために driver に **boot parameter `fpga.no-pl-wait=1`** を置く: `bsp_is_emulated()`（§1.4、`PSS_IDCODE` = 0）**と** この parameter の両方が真の時だけ、手順 4 の INIT の待ちと手順 9 の `PCFG_DONE` の待ちを飛ばし、`FPGA_STATUS` に `FPGA_STATUS_EMULATED` を立てて返す（`ENABLED` にはしない）。実機では parameter が有っても無視して警告を出す。`qemu-zynq.sh` の p008 の起動だけがこれを付ける。parameter は `docs/` の boot parameter の文書に p008 で足す。
- 版 3（L1）: QEMU zynq の GP0 の窓は何も無いが、machine が `ignore_memory_transaction_failures = true`（`xilinx_zynq.c` 467 行）なので読みは bus error でなく 0 を返す（版 2 の「bus の error」は誤り）。どちらにしても窓の試験は実機だけ。

#### 5.6.1 UAPI（`include/uapi/fpga.h`、新規）

```c
#define FPGA_IOC_BASE 'F'   /* 既存の ioctl の番号の付け方（include/uapi/ioctl.h）に合わせて p008 で確定 */
struct fpga_status { uint32_t flags; uint32_t error; uint32_t devcfg_status; uint32_t window_base; uint32_t window_size; };
/* flags: FPGA_STATUS_DONE 1, _INIT 2, _ENABLED 4（level shifter と reset の解除）, _PROGRAMMING 8,
   _EMULATED 16（版 3: QEMU で PL の待ちを飛ばした、§5.6） */
struct fpga_reg { uint32_t offset; uint32_t value; };
struct fpga_irq_wait { uint32_t timeout_ms; uint32_t count; };   /* count: 前の呼び出しから保留の割り込みの数（out、版 3 の §5.6.2） */
FPGA_PROGRAM  _IO        /* 貯めた bitstream を書く */
FPGA_STATUS   _IOR  struct fpga_status
FPGA_ENABLE   _IO   / FPGA_DISABLE _IO
FPGA_REG_READ _IOWR struct fpga_reg   /* offset は窓の中、4 byte 揃え */
FPGA_REG_WRITE _IOW struct fpga_reg
FPGA_IRQ_WAIT _IOWR struct fpga_irq_wait
```
（形は案。番号と macro の名前は既存の UAPI の ioctl（例 `include/uapi/backlight.h`）を p008 で読んで合わせる。host の試験で struct の大きさと offset を固定する。）

#### 5.6.2 block の窓（GP0）と PL の割り込み

- 窓: 物理 `0x43C00000`、64 KiB（§5.10 の block の address。boot parameter `fpga.window=BASE,SIZE` で GP0 `0x40000000`〜`0x7FFFFFFF`・GP1 `0x80000000`〜`0xBFFFFFFF` の中に変えられる）。`kern_device_map` で Device の属性に張る（`FPGA_ENABLE` の時に張り、`DISABLE`・`PROGRAM` の前に外す）。
- `FPGA_REG_READ/WRITE` は **`ENABLED` の時だけ**（それ以外は `EIO`）: PL が未設定か level shifter が閉じた時の GP0 への access は AXI の応答が無く CPU が止まり得る（未確認、Zynq の既知の注意）ので、kernel が守る。offset は窓の中で 4 の倍数、それ以外は `EINVAL`。access は 32 bit の 1 回（`hal_mmio_read32/write32`）。
- 割り込み: PL→PS の `IRQ_F2P[0]` = GIC の ID 61（SPI 29。`IRQ_F2P[7:0]` が ID 61〜68 という対応は UG585 未読の記憶で **未確認**: p003 で Linux の zynq の DT の PL の node の慣習か UG585 で確かめる）。`hal_irq_set_mode(61, HAL_IRQ_TRIGGER_LEVEL, HAL_IRQ_POLARITY_HIGH)`、handler は IRQ を mask して数を足し、`FPGA_IRQ_WAIT` の待ち手を起こす。待ち手（userland）は block の `IRQ_STATUS` を W1C で消してから次の `FPGA_IRQ_WAIT` を呼び、driver はその呼び出しで unmask する（level の割り込みが消える前に unmask して嵐にならない順）。`ENABLED` でない時は mask のまま。
- 版 3（L7）**`FPGA_IRQ_WAIT` の意味を決める**: driver は「前の `FPGA_IRQ_WAIT` が返ってから handler が走った回数」（保留の数）を持つ。呼び出しは (1) 保留が 0 なら IRQ を unmask して `timeout_ms` まで眠る、(2) 保留が 1 以上か起こされたら、保留の数を `count` に入れて 0 に戻して返る（handler が mask したまま返る。次の呼び出しまで unmask しない）、(3) timeout なら `ETIMEDOUT`、`count` = 0。待ち手は 1 つだけ: 2 つ目の同時の呼び出しは `EBUSY`。`ENABLED` でない時は `EIO`。§7.7 の 4 は別の process の `fpgareg write 0x20 2` の後に `fpgareg wait` が来ても、level の線は W1C まで立ったままなので、`wait` の unmask で直ちに handler が走り 1 回を返す（最初の割り込みを失わない）。`fpgareg wait --ack 0x18` は返った後に STATUS を W1C で消す。2 回目の `wait 100` は線が落ちているので `ETIMEDOUT`（嵐でない事の確かめ）。
- mmap（画面の RTL で要る）は後の WS。WS286 は ioctl の 1 word ずつで足りる。

#### 5.6.3 userland: `fpgaload`・`fpgareg`（`userland/base/fpgaload/`、1 つの dir で 2 つの program）

- `fpgaload FILE.bit|FILE.bin`（`.bit` の header（0x0009 の magic、a〜e の field、`e` の長さの後が data）を外し、word の byte を反転、sync word を確かめて `/dev/fpga0` に write → `FPGA_PROGRAM`）、`fpgaload --status`、`fpgaload --enable`、`fpgaload --disable`。
- `fpgareg read OFFSET`、`fpgareg write OFFSET VALUE`、`fpgareg wait [TIMEOUT_MS]`（割り込みを待ち、来たら `IRQ_STATUS` を読んで表示し W1C で消す。offset は §5.10 の表を知らない汎用の道具: `wait` の W1C の offset は `--ack OFFSET` で渡す、既定は §5.10 の 0x18）。
- 試験の script は shell で `fpgareg` を並べる（§7.7）。

### 5.7 reset・watchdog

`hal_reset` は §3.13（SLCR）。host からの reset は `jtag-reset.sh`（同じ `PSS_RST_CTRL` の書き込みを DAP から、§4.3）。SWDT（`cdns,wdt-r1p2`、`0xF8005000`）は WS286 では使わない。後の option: 「hang したら 60 s で自動 reset」の SWDT（`ZMR` の WDEN|RSTEN、`CCR` の counter、`RESTART` = 0x1999 を tick で蹴る）を `reboot=watchdog` の boot parameter で（jumper が JTAG の間は reset の後に BootROM が待つだけなので、SD の起動の WS で意味を持つ）。Values referenced: Linux `cadence_wdt.c`。

### 5.8 QSPI（後）

`xlnx,zynq-qspi-1.0`（`0xE000D000`）、linear の読み（`0xFC000000`）で boot の flash を読むだけの driver は WS286 の外（Future Work）。

### 5.9 platform の binding（`src/kern/platform/zybo.c`、`armv7-virt.c`）

- 2 つの新しい translation unit（`src/kern/platform/README.md` の「platform ごとに 1 file」の規則）。形は `riscv-virt.c`（serial の console と初期の ramdisk）: `kern_platform_init` は console（zybo: `cdns-uart.c`、virt: 新しい `src/drivers/serial/pl011.c`）、`hal_get_arch_handoff("armv7.initrd")` の範囲を `drv_ramdisk_create("ram0", …)` で boot device に（riscv-virt.c の `publish_ramdisk` と同じ確かめ: 両端が direct map の中）。zybo は `kern_platform_refresh_devices` で devcfg（`/dev/fpga0`）を attach。`kern_platform_reboot` → `hal_reset`、poweroff は unsupported（virt は PSCI）。
- SD・GEM・USB の attach は後の WS（版 1 の記述は §5.3〜§5.5）。

### 5.10 PS と PL の間の最初の block（`plio`）

**置き場所（案、U5）: `platform/armv7/zybo/pl/plio/`**（`plio.v`、`plio.xdc`、`README.md`、openXC7 の build は `plan/tools/zybo/pl-build.sh`）。zedBSD の `make` には入れない（host の FPGA の toolchain の物）。SPDX は `Zlib`（RTL は zedBSD の source）。

| 置き場所の案 | 判断 |
| --- | --- |
| `platform/armv7/zybo/pl/<block>/`（採る） | ZYBO の板に固有の hardware（pin の制約は板の物）で、platform の下の板の dir（`firmware.mk`・`disk-image.mk` と同じ所）に合う。新しい top の dir を作らない。後の画面の RTL も `platform/armv7/zybo/pl/video/` に並ぶ |
| `plan/ws286/rtl/` | WS が完了したら plan の dir は消す規則（AGENTS.md）。block は後の画面の RTL の土台で、製品の source |
| `userland/fpga/` | userland は zedBSD の toolchain で zedBSD の上の program を作る所。RTL はどちらでもない |
| 新しい top の `hw/` | 新しい source の root は Guardrail の変更が要る。板が 1 つの今は過剰。板が増えたら考え直す |

- 中身（`xc7z010clg400-1`、clock は PS の `FCLK_CLK0` = 100 MHz（`ps7_init` の `FPGA0_CLK_CTRL` DIV0 = 10、§1.2）、reset は `FCLK_RESET0_N`）:
  - `PS7` の primitive を instance し、`M_AXI_GP0` を block の slave に繋ぐ（`M_AXI_GP0_ACLK` に `FCLK_CLK0` を返す）。`IRQ_F2P[0]` に block の割り込み。
  - slave は **AXI3**（GP0 は AXI3: `AWID`/`ARID` 12 bit を `BID`/`RID` に返す、`WID`、`AxLEN` は 4 bit）。kernel は 32 bit の単発の access しかしないが、burst が来ても各 beat に `OKAY` を返し、read は同じ register を `AxLEN+1` 回返す（bus を止めない事が目的）。書き込みは `WSTRB` を守る。
  - 窓の 64 KiB の中の offset（下位 16 bit だけを解き、窓の外の alias は気にしない）:

| offset | 名前 | R/W | 中身 |
| --- | --- | --- | --- |
| 0x00 | ID | R | `0x504C494F`（"PLIO"） |
| 0x04 | VERSION | R | 1 |
| 0x08 | LED | RW | [3:0] = LD0〜LD3 |
| 0x0C | INPUT | R | [3:0] = SW0〜SW3、[7:4] = BTN0〜BTN3（2 段の同期） |
| 0x10 | SCRATCH | RW | 32 bit |
| 0x14 | COUNTER | R | FCLK0 の数（32 bit、free-running） |
| 0x18 | IRQ_STATUS | R/W1C | [0] = button の押下（立ち上がり）の印、[1] = software の印 |
| 0x1C | IRQ_ENABLE | RW | [1:0]。`IRQ_F2P[0]` = OR(STATUS[1:0] & ENABLE[1:0])（level） |
| 0x20 | IRQ_SET | W1S | [1] を立てる（人が button を押さずに割り込みの道を試す） |
| 他 | — | R | 0（書き込みは無視、応答は `OKAY`） |

  - pin（LED・SW・BTN）は Digilent の ZYBO の master XDC（Digilent の `digilent-xdc` の repository、MIT）から **p003 で取り Values referenced で引く**（この版では値を書かない: 記憶の値を使わない）。
- **openXC7 で `PS7` を place・route できるか**（nextpnr-xilinx の `PS7` の bel と prjxray の Zynq の database）は **未確認、WS286 の最大の risk**（R14）。p003 の最初の step で、(a) PS7 の無い LED の点滅（PL の clock は ZYBO の板の Ethernet PHY の 125 MHz の出力が PL の pin に来ている、と記憶するが **未確認**: XDC で確かめる。無ければ PS7 が要る）、(b) PS7 + GP0 の block、の順に build する。(b) が通らなければ p003 を uncleared にして Q1 に返す（ユーザー: 「難しいとなったらAMDのツールを導入します」）。
- zedBSD 無しの確かめ（p003、§7.5）: `jtag-boot.sh --spl-only` → OpenOCD の `zybo_pl_enable` → `pl-jtag.sh plio.bit` → OpenOCD で CPU0（MMU off、止めたまま）から `mdw phys 0x43C00000`（ID）、SCRATCH の書き読み、COUNTER が進む事、LED の書き読み。

## 6. 資源・並行性・失敗

- 所有: page table は space（HAL が割り当て、`hal_space_destroy` で L2 の table と L1 を pmem に返す。kernel の L1 は固定）。uncached の alias は呼び手。devcfg の buffer は `/dev/fpga0` の open file description（最後の close で解放）。窓の mapping は driver（`ENABLE` から `DISABLE`・`PROGRAM` まで）。ramdisk の memory は kernel の寿命。
- lock: GIC の distributor は spin（§3.4）、ASID の割り当ては spin、devcfg は 1 つの mutex（同時の program は `EBUSY`。`PROGRAM`・`ENABLE`・`DISABLE` と窓の access を同じ mutex で直列にし、窓を外す間に access しない）。`FPGA_IRQ_WAIT` は mutex を持たずに待つ（割り込みの数は spin で守る）。1 CPU なので driver の割り込みは CPU0。
- 割り込みと DMA: 全部 non-coherent（§3.12）。devcfg の DMA の buffer は uncached（`drv_dma_alloc_coherent`）。L2 は off（§3.11）。
- 失敗と回復: DTB が無い → 固定の address と既定の command line。ramdisk が無い・範囲が direct map の外 → boot device 無しで `VFS initialization failed` の段で止まる（riscv32 と同じ到達点）。PCAP の timeout → `ETIMEDOUT`、PL は未定義なので `ENABLED` にしない（窓は `EIO`）、`--status` で見える。PL の割り込みが嵐 → level のまま unmask しない順（§5.6.2）。panic は UART に register dump（`debug.c`）。板が応答しない → `jtag-reset.sh`、DAP も応答しなければ USB の抜き差し（ユーザー、R15。p002 では一度も要らなかった）。版 3 の追加: BSS が DTB・initrd と重なる → locore が `wfi` で止まる（gdbstub か JTAG で pc を見る、§2.2）。boot の直後の保留の async abort → 1 回だけ表示して続ける、それ以外の async abort → `MACHINE_CHECK`（§3.3）。`PSS_IDCODE` が知らない値 → 表示して実機として続ける（§1.4）。QEMU zynq の `reboot` → `ZYBO RESET FAILED` で止まる（§3.13）。

## 7. 試験の計画（版 3）

AGENTS.md の試験の方針（2026-10-03）に従い、細かい修正ごとには build（warning 0）と変えた所の短い host の試験だけ。QEMU と実機は意味のまとまり（各 Phase の受け入れ）で T1 にまとめて頼む。QEMU の証拠と実機の証拠は分けて書く。版 3（M11）: 各 Phase の受け入れは、その Phase が作った物を実際に通る試験で示す（`VFS initialization failed` だけでは task・signal・VFP を何も確かめないので、それらは HAL の self-test（§7.6）と user の試験の program `abitest`（§7.2）で示す）。

### 7.1 host（担当が流す）

- `make ZEDBSD_PLATFORM=armv7-virt vmunix`・`zybo`（warning 0）。`llvm-nm -u` で `__atomic_*` と、§2.2 の表に無い `__aeabi_*` が出ない（出た物で表を確定する）。`readelf -S` で `.ARM.exidx` が無く、text が RX の section に収まる。版 3（C1）: `readelf -h` の `e_entry` が platform の物理の入口（zybo `0x00100000`、virt `0x40010000`）、`llvm-nm` の `_start` が VMA の先頭（zybo `0x80100000`、virt `0x80010000`）、`vmunix.bin` の大きさが `_edata - _start` と合う。
- host の unit test（`plan/ws286/tests/`、`plan/tools/ports/mips-pte-host-test.c` の形）:
  - `pte-host-test.c`（版 3、H2・C1）: descriptor の組み立て（属性・AP・AF・XN・nG・ASID・TTBR の IRGN の bit 6/bit 0）、影の table の位置（page + 0x000 / + 0x800 / + 0xC00）、状態遷移 3 つ（論理 W・RO 中 → write fault → DIRTY・RW、本当の RO → write fault → kernel へ `PAGE_FAULT`、AF=0 → AF fault → ACCESSED）、`prot_query` の write の取り消しで DIRTY が返る事、`PHYS_BASE` 0 と `0x40000000` の両方で `pmem_to_kernel` の範囲の端。
  - `aeabi-host-test.c`（版 3、M1）: int64.c の armv7 の C の部分の算術（divmod の商と余りの両方、境界の値）。
  - `devcfg-host-test.c`（版 3、M4）: `CTRL` の helper が mask の外を拒む、`UNLOCK` の定数、DMA の 8 byte 揃え・4 の倍数の確かめ、`INT_STS` を消す値。
  - `bitstream-host-test.c`（`fpgaload` の `.bit` の header の解析と byte の反転、sync word。入力は自分で作った小さな header + data、bitstream の本物は要らない）、`fpga-uapi-host-test.c`（`include/uapi/fpga.h` の struct の大きさ・offset）。
- 道具: `bash -n` と（有れば）`shellcheck` を新しい `plan/tools/zybo/*.sh`（p005 の `qemu-zynq.sh` など）に。DTS の雛形を `dtc` で compile し、`fdtget` で `bootargs`・`linux,initrd-start` を読み戻す。

### 7.2 QEMU virt（T1、`boot-test.sh` の `BOOT_MODE=armv7-virt`、PNG の判定 = 既定の規則）

- [proposed/boot-test-armv7.md](proposed/boot-test-armv7.md)（版 3: raw の `vmunix.bin`、`gic-version=2`）の mode を Q1 が足す。HAL が ramfb に描く console の文字を `boot-test.py` が読む（riscv32-virt と同じ）。`BOOT_PATTERN` で各 Phase の文字列、`BOOT_THEN` の `type:` で serial（PL011）から打つ。
- p004 の最初の T1 の依頼に §4.6 の gdb の確かめ（`r2` が DTB の magic を指す、`/chosen` の `bootargs`）を入れる。
- 到達点:
  - p004: `ARMV7 HAL READY`・`AEABI PASS`・`ATOMIC PASS`・`PAGING PASS`・`TIMER TICK`。
  - p006: `VFP PASS`（HAL の self-test）と `VFS initialization failed`。
  - p007: `abitest` を init にした最小の root（`INITRD=abi.img`、`BOOT_APPEND='init=/abitest'`）で `ABI PASS`。
  - p010: `INITRD=root.img` の `login:`、`type:root`・`type:uname -m` → `armv7`。
- `abitest`（版 3、`plan/ws286/tests/abitest.c`、zedBSD の libc で動的に link する普通の program。p007 で書く）が順に確かめて 1 行ずつ `/dev/console` に出す: (1) syscall の往復（`getpid`・`write` の戻り値）、(2) TLS（`__thread` の変数を 2 つの thread で別の値、errno）、(3) `fork` と `waitpid`（子の終了の値）、(4) VFP: 親と子が d0〜d31 と FPSCR に別の値を置き、互いに `sched_yield` を挟んで 1000 回 switch しても値が保たれる、(5) signal: `SIGALRM` の handler が VFP の register を壊して戻っても、割り込まれた側の値が保たれる、handler の中の `sigreturn` の道、(6) `execve` の後の子で VFP が初期値（FPSCR 0）、(7) `uname -m` 相当（`uname(2)` の machine が `armv7`）。全部通れば `ABI PASS`、どれかが落ちれば `ABI FAIL <番号>`。VFP の値は inline asm で置いて読む（compiler の最適化に頼らない）。
- 解析は gdbstub（`QEMU_EXTRA='-S -gdb tcp::PORT'`、host の `gdb-multiarch`、`set architecture armv7`、`file build/armv7-virt/vmunix`）。

### 7.3 QEMU xilinx-zynq-a9（T1、`plan/tools/zybo/qemu-zynq.sh`、serial の判定 = **U3 の例外**、2026-10-23 決定）

- zynq の machine には表示の device が無い（PCI も fw_cfg も無いので ramfb も付かない）ので、PNG で判定できない。**生きた serial の line を対話で待つ**（`serial.py --socket S expect …` か p002 の `uart-expect.py` の socket 版。log の file を後から読まない）。
- §1.4 の制限（版 3 で直した）: timer は SLCR を見ず 100 MHz 相当（`PSS_IDCODE` = 0 で判別）、PL310 は register の箱だけ、`A9_CPU_RST_CTRL` と `PSS_RST_CTRL` は何もしない、ELF の boot で `r2` は渡らない（`-device loader` で DTB と ramdisk を置く、§4.6）、UART は TX 無効で始まる（§5.1）、GP0 は 0 を返す、devcfg は PCFG_INIT・PCFG_DONE を立てない（§5.6）。→ QEMU zynq で確かめるのは BSP の道（Cadence UART の初期化、SLCR の読み、判別、private/global timer、GIC の base、DTB の fallback、ramdisk、devcfg の register の道）だけ。PL の block と実際の clock は実機。
- 到達点: p005 `ZYBO PLATFORM qemu idcode=0x00000000`・`ARMV7 HAL READY`・`AEABI PASS`・`ATOMIC PASS`・`PAGING PASS`・`TIMER TICK` と、`TIMER 10S` の行が host の時計で 10 s ± 2%（QEMU の時計の揺れを見込む）、p006 `VFP PASS`・`VFS initialization failed`、p007 `ABI PASS`、p010 `login:`、p008 `fpga.no-pl-wait=1` で `fpgaload --status` と `fpgaload plio.bit` が `EMULATED` を返す（窓は試さない）。

### 7.4 実機（JTAG、`jtag-boot.sh --expect`、serial の判定 = **U3 の例外**、流す人は **U4**: 2026-10-23 決定、p002〜p003 は担当、p005〜p010・p008 の受け入れは T1）

- 板への操作は `jtag-reset.sh`・`jtag-boot.sh`・`pl-jtag.sh` だけ（lock を取る）。判定は生きた UART の line（`uart-expect.py`、Q1 の `serial.py --device` が来たらそれ）。証拠は表示した行の抜粋、OpenOCD の出力（load の SHA-256・速さ・breakpoint の pc）。電源は切らない（SwitchBot のプラグは ZYBO に未接続、どの道具も ZYBO のために使わない）。
- 到達点: p002（済み、§4.8）; p005 `ZYBO PLATFORM board idcode=0x13722093`・`ACTLR`/`SCU_CTRL` の値（SMP|FW と SCU の enable が立つ）・`ARMV7 HAL READY`・`AEABI PASS`・`ATOMIC PASS`・`PAGING PASS`・`TIMER TICK`、`TIMER 10S` の行の到着を host の時計で 10 s ± 1%、3 回続けた reset + boot、`ARMV7 PENDING ASYNC ABORT` が出たかの記録; p006 `VFP PASS`・`VFS initialization failed`; p007 `ABI PASS`; p010 ramdisk の root で `login:` → `root` → `uname -m`; p008 §7.7。
- JTAG の load は約 255 KiB/s（§4.8）なので、ramdisk は小さく保つ（p007 の `abi.img` は 2 MiB 以下、p010 の `root.img` は 8〜16 MiB、1 回の起動の load は 60 s 以下を目安）。
- 人の目が要る確かめ（LED が光る、switch・button の値）は **ユーザーが拾える試験の一覧**に書き、今は頼まない（memory「No test requests; list instead」）。自動の確かめは register の読み戻し・COUNTER・software の割り込み。

### 7.5 PL の block を zedBSD 無しで（p003、実機、U4）

`jtag-boot.sh --spl-only` → `pl-jtag.sh plio.bit` → OpenOCD `zybo_pl_enable`（版 3、L4: Linux の順。書いてから level shifter と reset）→ OpenOCD の `zynq.ahb`（AHB-AP、版 3）で `mdw 0x43C00000` = `0x504C494F`、`mww 0x43C00010 0x12345678` → 読み戻し、`0x43C00014` を 2 回読んで増える、`0x43C00008` の書き読み。`pl-jtag.sh` の後の devcfg の `INT_STS`（`0xF800700C`）の bit 2 を記録する（§5.6 の attach の扱い）。LED の目視はユーザーの一覧へ。

### 7.6 HAL の self-test（boot で走る。mips の `src/hal/mips/selftest.c` に倣う）

- `AEABI PASS`（版 3、M1）: §2.2 の表の symbol を asm から直に呼び、register の約束（`__aeabi_uidivmod` の r1 の余り、`__aeabi_uldivmod` の r2:r3、`__aeabi_memset(dest, n, c)` の順、`__aeabi_memclr`）を確かめる。
- `ATOMIC PASS`（版 3、H3）: §3.2 の `ldrex/strex` の試し。
- `PAGING PASS`: kernel mode から user の address（TTBR0）を使って: (1) 4 KiB の map と読み書き、(2) AF=0 の page の最初の読みで access flag fault が HAL の中で解決されて `ACCESSED` が立つ、(3) 論理 W で map した page（hardware は RO）への store で permission fault → `DIRTY` が立って書ける、(4) `hal_space_prot_query` で write を外すと `DIRTY` が返り、次の store は fault、(5) ASID を変えて同じ仮想が別の物理を指す、(7) direct map と uncached の alias で device 窓の書き込みが RAM に届く、(8)（版 3）論理 RO の page への store は `PAGE_FAULT` で kernel に出る（self-test の fault hook が受ける）。（6）の CPU1 からの見え方は SMP の後の WS。落ちたら `PAGING FAIL <番号>` で止まる。
- `VFP PASS`（版 3、M6、p006）: mips の `check_float`（`selftest.c` 668 行〜）と同じ形。2 つの試験の task の状態に別の d0〜d31・FPSCR を置き、lazy の切り替え（FPEXC.EN=0 からの Undefined の道を、kernel の試験の stub の VFP の命令で通す）・sync・forget を通して値が保たれ、forget の後は owner が無い事を確かめる。

### 7.7 PL を zedBSD から（p008、実機）

ramdisk に `fpgaload`・`fpgareg` と `plio.bit`（U6 は (a): `pl-build.sh` の出力を image の `--file` で複写、2026-10-23 決定）を入れ、UART の生きた line で:
1. `fpgaload --status` → DONE 0。`fpgareg read 0` → `EIO`（`ENABLED` でない時の守り）。
2. `fpgaload /usr/share/fpga/plio.bit` → DONE 1・ENABLED。`fpgareg read 0` = `0x504c494f`、`read 4` = 1。
3. SCRATCH の書き読み（3 つの値）、COUNTER が 2 回の読みで増える、LED の書き読み。
4. `fpgareg write 0x1c 2`、`fpgareg write 0x20 2`、`fpgareg wait --ack 0x18 1000` → 割り込み 1 回、STATUS[1] が消える。2 回目の `wait 100` は `ETIMEDOUT`（嵐でない、§5.6.2 の L7 の意味）。
5. `fpgaload --disable` → `read 0` が `EIO`。JTAG の道: `pl-jtag.sh plio.bit`（zedBSD が走ったまま）→ `fpgaload --enable`（INT_STS の PCFG_DONE が立たない板なら `--force`、§5.6）→ 2〜4 をもう一度。
6. ユーザーの一覧: LED が `fpgareg write 8 5` で LD0・LD2 だけ光る、SW・BTN を動かして `read 0xc` が変わる、BTN で `wait` が返る。

## 8. Phase と LW（版 3、8 LW ≈ 2〜3 時間）

版 3（M11）の方針: 依存には決定済みの判断も残す（U1・U3〜U6 は 2026-10-23 に決定、§10）。U3 は p005・p006・p007・p010・p008 の QEMU zynq と実機の受け入れの全部に効く。

| Phase | 目的と到達点 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | この設計（版 1 6 + 版 2 4 + 版 3 4） | 14 | — |
| p002 | **JTAG の道具と SPL**（cleared 2026-10-23、[phase002](phase002/phase.md)、§4.8） | 8 | U4 |
| p003 | **PL の block（kernel 無し）**: `platform/armv7/zybo/pl/plio/`（§5.10、U5）、`pl-build.sh`（openXC7）、`pl-jtag.sh`、§7.5。最初に PS7 の place・route の可否（R14） | 8 | p002、**openXC7 の導入（ユーザー）**、U5 |
| p004 | **HAL の入口（virt、1 CPU）**: `include/hal/arch.h`・`arch/armv7.h`（U1）、`src/hal/armv7/` の locore（`PHYS_BASE`、一時の identity、BSS の前の DTB/initrd の重なりの確かめ）・vectors（Linux 形の入口、8 byte 揃え、monitor の閉じ）・page（影の table）・space（ASID）・int（例外の分類）・gic・clock・lib・cmain・debug・selftest・aeabi.S、bsp-virt（PL011、FDT、GICv2、generic timer、ramfb の console、PSCI の method）、`platform/armv7/`（toolchain.mk・vmunix.mk（`-DKERN_USER_ABI_ARMV7`）・vmunix.ld（platform ごとの address）・Makefile）、`Makefile`・`menuconfig.py` の `armv7-virt`・`zybo`、`config/ci/config-armv7-virt.mk`・`config-zybo.mk`、`int64.c` の armv7 の節、`stat.h` の枝と `#error`、host 試験（pte・aeabi）。到達点: virt の画面に `ARMV7 HAL READY`・`AEABI PASS`・`ATOMIC PASS`・`PAGING PASS`・`TIMER TICK`、gdb で `r2` の確かめ（T1） | 16 | **Q1: LLVM に ARM と armv7 の triple（1 回の作り直し、cache asset の upload はユーザー）**、**Q1: boot-test の `armv7-virt`（版 3 の案）**、U1 |
| p005 | **zybo の BSP を JTAG で**: bsp-zybo（Cadence UART の early console と QEMU の初期化、memory（先頭 1 MiB を外す）、SLCR の読み、`PSS_IDCODE` の判別、SCU と ACTLR.SMP|FW、erratum 754322 の順、`cpsie a`、private/global timer、DTB の fallback、`hal_reset` の QEMU の park）、`qemu-zynq.sh`、`jtag-boot.sh` の kernel の load の直し（raw の時の `_end`）。到達点: §7.3・§7.4 の p005 | 12 | p002、p004、**U3**、`uart-expect.py`（p002、済み）か Q1 の `serial.py --device` |
| p006 | **task・syscall・signal・VFP・cache（両 BSP、1 CPU）**: int.c の syscall、task.c の switch/fork/exec/signal・TLS（TPIDRURO）、fpu.c の lazy（sync・forget、やり直しの Undefined）、`hal_cpu_idle` の順、cache.c（L1、L2 は `CONTROL` を読むだけ）、`armv7-virt.c`・`zybo.c` の platform（console と ramdisk の口）、`cdns-uart.c`・`pl011.c` の kernel の driver、`VFP PASS`。到達点: virt（PNG）・zynq・実機で `VFP PASS` と `VFS initialization failed`。**user mode・syscall・signal・fork の受け入れは p007 の `abitest`**（この Phase では user の program が無いので確かめられない、と明記して clear する） | 14 | p005、U3 |
| p007 | **userland の runtime（動的 PIE、§2.3）**: sysroot.mk の armv7 と builtin の manifest（Q1）、crt0/crt1、`syscall-armv7.S`・`setjmp-armv7.S`、`static-tls.c`・`posix.c`（`armv7`）・`pthread.c` の枝、rtld の ARM 版（`entry-armv7.S`、`R_ARM_*`、REL、variant I の動的 TLS）、`elf.c` の `EM_ARM`、`platform/armv7/userland.mk`、`abitest` と最小の root（`abi.img`: `/lib/ld.so`・`/lib/libc.so`・`/abitest`、`init=/abitest`）。到達点: §7.2〜§7.4 の `ABI PASS`（virt PNG・zynq・実機） | 20 | p006、**Q1: toolchain 案 2（p004 の作り直しに含める）と sysroot.mk**、U3 |
| p010 | **ramdisk の root と login**（版 3 で p007 から分けた）: `platform/armv7/rootfs.mk`（riscv64 の `make-riscv64-root-image.py` の形、UFS 8〜16 MiB）、最小の program の組（`ZEDBSD_USER_PROGRAMS := sh login ls cat echo uname dmesg` と `fpgaload` の置き場所）、`config-zybo.mk`。到達点: virt（`INITRD`）の `login:` → `root` → `uname -m` = `armv7`（PNG）、QEMU zynq と実機の JTAG の ramdisk で同じ（UART） | 8 | p007、U3 |
| p008 | **PL を zedBSD から**: `zynq-devcfg.c`（版 3 の手順、`CTRL` の helper、`fpga.no-pl-wait`）、`include/uapi/fpga.h`、`/dev/fpga0`（Q1 の devfs の行）、`fpgaload`・`fpgareg`、PL の割り込み（ID 61、`FPGA_IRQ_WAIT` の意味）、host 試験（devcfg・bitstream・uapi）。到達点: §7.7 の 1〜5（実機）、§7.3 の p008（QEMU zynq）。**WS286 の到達目標** | 13 | p003、p010、**Q1: devfs の `fpga`**、U3、U6 |
| p009 | 規約の全文の見直し（conformance） | 6 | p002〜p008、p010 |

合計 **119 LW**（約 30〜45 時間、p001 の 14 と cleared の p002 の 8 を含む。残りは 97 LW）。版 2 の 97 LW からの増: p001 +4、p004 +2（影の table・aeabi・self-test・platform ごとの address）、p005 +2（SCU・ACTLR・判別・UART の初期化）、p007 +4 と p010 +8（rtld の ARM 版と libc の arch の file、H4: review の推定 8〜12 LW の増と同じ程度）、p008 +1、p009 +1。critical path は Q1 の LLVM（ユーザーの cache の upload を含む）→ p004 → p005 → p006 → p007 → p010 → p008 → p009。p003 は openXC7 が入れば p004〜p010 と並行できる（別の担当でもよい）。

後の WS に回す物（WS286 の外、Q1 が WS を作る。設計は §3.7・§3.11・§5.3・§5.4・§4.7 に残した）: SMP の CPU1（8 LW、erratum 764369 を含む）、L2 の PL310（4）、SD と SD の image（12）、LAN と SSH（14）、SD の起動の BOOT.BIN（8）、`cacheflush` の syscall（JIT の WS、erratum 775420 の規則を含む）。USB は WS287（Q1）。

## 9. risk と敵対的な読み直し（版 3）

| # | risk | 対処 |
| --- | --- | --- |
| R1 | LLVM の ARM の target が共有の toolchain に無い（確認済み）。Q1 の作り直しと、ユーザーの cache asset の upload を待つ | p003 は LLVM 不要で先に進む。案 1 と案 2 を 1 回の作り直しにして 2 回待たない |
| R2 | `__aeabi_*` の wrapper（asm）と 64 bit の atomic の inline が揃わず link が落ちる | p004 で `llvm-nm -u` を受け入れ条件に（§2.2 の表を確定）。落ちたら ENTER style と aeabi.S |
| R3 | CPU1 の BootROM の待ち方 | p002 で実機の code を読んで確かめた（§4.8）。WS286 は CPU1 を起こさない |
| R4 | QEMU zynq の timer が SLCR を見ない（100 MHz）ので実機と周波数が違う | 版 3: 判別は `PSS_IDCODE`（§1.4、実機 0x13722093）。QEMU と実機の両方で tick の速さを host の時計で測る |
| R14 | **openXC7 が Zynq の `PS7` を place・route できない**（未確認）と、PS と PL の block が OSS で作れない | p003 の最初の step。駄目なら p003 を uncleared にして Q1 に返す（ユーザーは AMD の tool を入れると言っている）。kernel の側（p004〜p010）は影響を受けない |
| R15 | DAP からの system の reset が DAP 自身も落とす | p002 で確かめた: 落ちるが次の process で立て直せる（§4.8） |
| R16 | SPL が breakpoint の前に DDR の不定の中身へ飛ぶ | p002 で `CONFIG_SPL_RAM_DEVICE` を外した SPL にし、外れても `hang()`（§4.8） |
| R17 | JTAG の load が遅く、ramdisk の大きさで 1 回の起動が長くなる | 実測 約 255 KiB/s（§4.8）。`abi.img` 2 MiB 以下、`root.img` 8〜16 MiB（§7.4） |
| R18 | 未設定の PL・閉じた level shifter への GP0 の access で CPU が止まる | `ENABLED` の時だけ窓を許す（§5.6.2）。p003 の試験は AHB-AP で、`zybo_pl_enable` の後 |
| R19 | OpenOCD が interface 0 を外す時に `/dev/ttyUSB*` の番号が変わる | UART は by-id の path |
| R20 | CPU1 が BootROM の `wfe` から `sev` で起き、`0xFFFFFFF0` の値へ飛ぶ | WS286 の HAL は `sev` を使わず（spin は `yield`）、PA `0xF0000`〜`0xFFFFF`（高位 OCM の alias）と `0xFFFFFFF0` に書かない（§3.12） |
| R21 | serial の判定（U3）が「log を読んで判定しない」規則に触れる | U3 は 2026-10-23 に決定。生きた line の対話に限り、log の file を後から読まない。virt は PNG のまま |
| R22 | `ps7_post_config` の無い状態で PL の clock・reset が想定と違う | devcfg の `ENABLE` が level shifter と reset を明示に設定。FCLK0 は 100 MHz を p002 で確認（§4.8） |
| R23（版 3） | virt の raw の image で、BSS が initrd か DTB と重なる（QEMU は file の終わりの後ろに置く） | 512 MiB では initrd は 128 MiB 目。locore が BSS を消す前に重なりを確かめて止まる（§2.2） |
| R24（版 3） | virt は Non-secure、zybo は Secure で入るので、HAL の secure 専用の操作が virt で未定義命令になる | SCU・ACTLR は bsp-zybo に閉じ込める（§3.2）。GIC は両方で banked の register だけ使う。NSACR は QEMU の firmware の真似に頼り `VFP PASS` で確かめる |
| R25（版 3） | `PSS_IDCODE` が QEMU で 0 でない（QEMU の版の違いなど） | `ZYBO PLATFORM …` の行で見える。QEMU と判定できなければ timer は SLCR から 162.5 MHz と計算し、`TIMER 10S` の時計の測りで落ちる（黙って通らない） |
| R26（版 3） | rtld の ARM 版の relocation・TLS の誤りが、最初の user の program まで見えない | p007 の最初は `abitest` 1 つと `ld.so`・`libc.so` だけの `abi.img`。`llvm-readelf -r` で使う reloc の種類を先に数え、表に無い物は rtld が名前を出して止まる（riscv64 と同じ `rtld_fatal`） |
| R27（版 3） | erratum 754322 などの A9 の誤りは QEMU で再現しない | 対処の命令は Linux と同じ所に置く（§3.5）。実機の受け入れ（p005〜p008）で間接に確かめる |
| R5〜R13 | 版 1 の SD・LAN・USB・L2・SMP の risk | 後の WS に移した（版 1 の表は git の履歴） |

敵対的に読み直した点（版 1 の 10 点は §3 の設計に残る）。版 2 の追加: (11) **JTAG の流れの memory の地図は U-Boot の後と違う**: 版 3 で p002 の実測に置き換えた（§3.12・§4.8）。(12) **SPL の BSS（`0x100000`〜）と kernel の load の場所が重なる**: SPL を止めた後に load するので良い。止める前に load しない（`jtag-boot.sh` の順）。(13) **`resume` の時の CPSR と cache**: SPL の中の任意の点で止めた CPU に入口を与えるので、CPSR を明示し、I-cache を invalidate する。MMU は SPL が使わない（`DCACHE_OFF`）が、kernel の `locore.S` は SCTLR を確定した値に書く（M・C を落としてから自分で立てる）。(14) **`target smp` にしない理由**: smp にすると `halt` が CPU1 も止め、`resume` が CPU1 も BootROM の外の状態で動かし得る。(15) **DTB を r2 で渡せない QEMU zynq**: `0x01F00000` の fallback は magic を確かめる。JTAG も同じ場所に置く。(16) **`board_boot_order` が inline される可能性**: p002 で symbol が残る事を確かめた（`0x00000fd8`）。(17) **devcfg の `UNLOCK`**: JTAG の流れでは誰も書かないので kernel が書く（§5.6 の 1）。(18) **userland の hard-float**: kernel は VFP を使わないが user は使う → p006 の lazy な VFP は login の前に要る（p006 に入れた）。
版 3 の追加（review の後に自分で読み直した点）: (19) **raw の image と ELF の 2 つの入口**: virt は raw だけ、QEMU zynq は ELF だけ、実機は両方を受ける。入口の register の約束は 1 つ（`r2` = DTB、無ければ zybo だけ `0x01F00000` を見る）で、HAL は `r0`・`r1` を見ない。(20) **影の table と kernel の page**: kernel の image の L2（W^X）と device 窓の L2 も同じ 1 page の形で割り、影の側は kernel では `PRESENT` と属性だけ使う（形を 1 つにして page.c の分岐を減らす）。(21) **`hal_space_prot_query` の正しさの根拠**: hardware が DIRTY を立てない以上、DIRTY でない page への store は全部 fault を通るので、TLB の invalidate の後に影の DIRTY を読めば足りる（mips の M1〜M3 と同じ論理）。(22) **例外の frame の 20 word**: user の frame の位置が変わるので、`hal_task_*` の debug の register の読み書き・signal の frame の写しは frame の構造体 1 つ（`struct armv7_frame`）からだけ読む。(23) **dummy の `strex` の address**: Linux と同じく frame の下の使わない word（`sp - 4`）にし、user の memory に触れない。(24) **`fpga.no-pl-wait` の誤用**: 実機で有効にならない事を二重の条件で守る（§5.6）。

## 10. ユーザー・Q1 の判断（版 3）

| # | 問い | 勧め | 状態 |
| --- | --- | --- | --- |
| U1 | HAL の API の差分（§3.13 の 3 点）を WS263〜272 と同じ形で事前承認するか | 同じ形で事前承認（案 [proposed/hal-armv7.md](proposed/hal-armv7.md)） | **決定**（2026-10-23 ユーザー「U項目は読みましたが、全部推しでOKでした。」）。版 3 で追加の HAL の API の変更は無い |
| U2 | USB の範囲 | — | **解決**（2026-10-23 ユーザー: WS286 から外す、LS/FS は WS287） |
| U3 | ZYBO（QEMU zynq と実機）は **生きた serial の line の対話**で判定してよいか | 認める。virt は PNG のまま | **決定**（同上、AGENTS.md の検証の例外） |
| U4 | 板（JTAG・UART）を誰が触るか | p002・p003 は担当が直に、kernel の受け入れ（p005〜p010・p008）は T1 | **決定**（同上） |
| U5 | 最初の PL の block の RTL の置き場所 | `platform/armv7/zybo/pl/plio/`（§5.10 の表） | **決定**（同上） |
| U6 | bitstream を試験の ramdisk に入れる道 | (a)（WS286 の間）、配布する時に (b) | **決定**（同上） |
| U7 | user の code は ARM mode だけ（Thumb-2 は Future Work） | ARM だけ | **決定**（同上） |
| U8 | WS286 の完了の条件を絞り、SMP・L2・SD・LAN・SD の起動を新しい WS に移す | 移す（§8 の後の WS の表） | Q1（ws.md の目的に反映済み） |
| — | 版 3 で新しくユーザーの判断が要る物 | — | **無い**。toolchain の変更は Q1 の許可、LLVM の cache asset の upload（GitHub への公開）はユーザーの操作（下の 1） |

**Q1 への依頼**（版 3）:
1. **toolchain**（Q1 の許可、担当は適用しない）: 共有の LLVM に `ARM`（`llvm.mk` 361 行の `LLVM_TARGETS_TO_BUILD` に `;ARM`）と zedBSD の armv7 の triple（`0001-add-zedbsd-x86-target.patch` に §2.1 案 2 の 60〜80 行）を **1 回の作り直し**で、`version.mk` `zedbsd10` → `zedbsd11`。memory の「LLVM patch level needs new cache」により **rev-0 の cache asset の upload と digest の更新を同じ commit で**行う必要があり、upload は GitHub への公開なので **ユーザーの指示が要る**（誰がいつ upload するかを Q1 がユーザーに聞く）。p004 の前に要る。大きさの見積もり（未計測）: 4 並列で 30〜60 分、`build/llvm-build`（今 1.6 GB）が 200〜300 MB 増、`clang` の binary（今 177 MB）が 15〜25 MB 増。sysroot.mk の armv7 の呼び出しと ARMV7 の 2 つの manifest（§2.3、compiler-rt の `arm/*` は既存の監査の境界と同じ license）は p007 の前（Q1 の許可）。
2. `boot-test.sh` の `armv7-virt` の mode（[proposed/boot-test-armv7.md](proposed/boot-test-armv7.md) **版 3**: 既定の image は raw の `vmunix.bin`、`gic-version=2`）。版 2 の案は適用しない（review M11 の 5）。
3. `plan/tools/guest/serial.py` に `--device PATH [--baud N]`。p002 の `uart-expect.py` が当座の代わりになるので、p005 の前には必須でない。
4. `src/kern/devfs.c` の名前の表に `fpga`（p008 の前）。
5. Master の Tools に `plan/tools/zybo/*`（p002 で作った）。
6. 後の WS の作成（SMP・L2・SD・LAN・SD の起動）と WS287（USB の split）。
7. 版 3 の p006・p007 の範囲の変更と p010 の新設の記録（ws.md は版 3 で直した。Queue・Master の写しは Q1）。

## 11. 選ばなかった案と理由

- **LPAE（長い descriptor）**: A9 に無い。短い descriptor 一本。
- **Thumb-2 の kernel/user**: 速さは要らず、例外の入口と signal の handler の扱いが増える。
- **FSBL（MIT）+ Vivado の ps7_init**: Vivado の導入と生成物の license の確認が要り、kernel の更新の運用（SSH で `uvmunix` を替える）に U-Boot が要るので U-Boot 一本。
- **ELF を U-Boot の `bootelf` で**: `xilinx_zynq_virt_defconfig` に `CMD_ELF` が有るか未確認で、DTB の受け渡し（`bootm` の r2）が無い。uImage + `bootm`。
- **GEM の core を rp1-ethernet.c から切り出す**: WS278 が同じ file を動かしている最中で衝突するので、新しい core を zybo から使い、RP1 の乗せ替えは後。
- **SDHCI の共通化を今やる**: 同じ理由（rpi4 の回帰）。写しで始める。
- **IPI の TLB shootdown**: A9/A15 の broadcast で足りる。
- **CPU1 を SLCR の reset だけで起こす**: QEMU で効かない（§1.3）ので、park と OCM を主にし reset は fallback。
- **PNG の boot test を全部の mode で**（版 2 で改めた）: virt は ramfb を付けて PNG（既定の規則）。zynq の QEMU と実機だけ表示が無いので serial（U3）。版 1 は virt も serial にしていた。
- **SD の起動を先に**（版 1）: jumper と SD の書き換えが毎回要る。JTAG が先（§12、§4.1 の表 E）。
- **upstream の `target/zynq_7000.cfg` を source する**: 版 3 で理由を直した: IDCODE は upstream の列に有った（§4.8）が、upstream の cfg は 2 core を `target smp` にする。値だけ引いて自分の cfg に書く。
- **`target smp` で 2 core を扱う**: CPU1 を BootROM の待ちから動かし得る（§9 の (14)）。
- **SD か LAN を PL の前に**: PL の動作に要らない（ユーザーの決定「最短で」）。root は JTAG の ramdisk、操作は UART の shell。
- **PL の窓を userland に mmap で渡す**（WS286 で）: 1 word ずつの ioctl で受け入れに足りる。画面の RTL の WS で mmap を足す。`/dev/mem` のような汎用の物理 memory の口は作らない（窓を PL が有効な時だけに限れないため）。
- **kernel の中の self-test だけで PL を試す**（userland 無し）: さらに短いが、ユーザーの目標の「fpgaload で書く」と「zedBSD から読み書き」を userland の道で示せない。kernel の self-test は p008 の解析用に boot parameter で残してよい（任意）。

- 版 3 で選ばなかった案:
  - **virt を ELF のまま入れ、DTB を RAM の先頭の決め打ち・ramdisk を `-device loader` で置く**（review C2 の (b)）: `r2` の約束が JTAG・U-Boot と分かれ、DTB の場所と ramdisk の範囲を `-append` で別に渡す道が要り、boot-test の `INITRD` の hunk も特殊になる。raw の image なら QEMU の Linux の約束が `r2`・`/chosen`・`-initrd`・`-append` を全部そのまま渡す。
  - **virt に uImage（OS = Linux）を渡す**（QEMU の `load_uimage_as` も Linux の道になり、load の address を zybo と同じ offset `0x100000` にできる）: QEMU の `hw/core/loader.c` を読んでいない事と、p004 で header を作る道具が要る事。raw は `boot.c` だけで確かめられた。
  - **QEMU zynq も raw の Linux の約束で入れる**: §4.6（置き場所の違いと Non-secure）。
  - **1 つの kernel の binary で virt と zybo の両方**（`PHYS_BASE` を実際の pc から求める位置独立の入口）: platform は別の build（`ZEDBSD_PLATFORM`）なので、定数の方が単純で誤りにくい。
  - **WS286 の userland を `-static` にする**（rtld の ARM 版を後に回す）: kernel の `load_static_tls` が ARM に無く（`elf.c`）、variant I の static TLS は m68k の形（TCB と TP の距離）を ARM の TP の offset 8 に合わせて kernel・libc・`uapi/tls.h` に足す事になり、rtld の ARM 版と同じ程度の手間で、他の arch（arm64・riscv64）の検証済みの動的の道から外れる。
  - **UP の Linux と同じ非 Shareable の属性**: §3.2（H3）。
  - **TEX remap で software の bit を作る**: §3.2（H2）。
  - **`uname -m` を `armv7l`**: §2.3。
  - **devcfg の DMA を複数の buffer の列で queue を見ながら積む**（Linux の形）: 7Z010 の bitstream は 2.08 MB で 1 つの物理連続の buffer に収まり、1 回の転送なら queue の満杯を扱う code が要らない。
  - **QEMU で devcfg の手順を通すために QEMU の検出だけで自動的に待ちを飛ばす**: 暗黙に振る舞いが変わる。boot parameter と検出の二重の条件にした（§5.6）。

## 12. 決定の記録（2026-10-23、Q1 経由。原文と、この設計での扱い）

| # | ユーザーの決定（原文） | 解釈と扱い（版 2） |
| --- | --- | --- |
| D1 | 「次の計画です。DIGILENT ZYBOボードにzedBSDを移植します。…JTAGからのブートはできたらうれしいものの、まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。PLのプログラミングは、SSH経由でできるようにできそうですか？」（ws.md の出典） | 版 1 の前提。起動の順は D3 で、SwitchBot は D2 で、SSH は D4 で置き換わった。PL の書き込みの道（devcfg + `fpgaload`）は残し、SSH が来れば同じ道具がそのまま使える（§4.5） |
| D2 | 「SwitchbotはまだZYBOに接続されていません。USB給電になっています。JTAGからリセットできるならそっちの方がいいですね。ひとまず、openFPGALoaderやopenxc7など、OSSのみで実装を進めてください。難しいとなったらAMDのツールを導入します。」 | reset は DAP から SLCR（§4.3、`jtag-reset.sh`）。道具は OpenOCD・U-Boot SPL・openFPGALoader・openXC7（§4.1・§5.10）。Vivado/xsdb は使わない（§4.1 の表 D）。openXC7 で PS7 が作れない時は止めて Q1 に返す（R14） |
| D3 | 「JTAGが簡単に利用できるなら、SDブートよりも先にJTAGブートを実装してください。」 | JTAG の起動を p002（kernel 無しで先に）と p005（kernel）に置き、SD の起動は後の WS（§4.7） |
| D4 | 「USBは後回しにして、full speed, low speedはWSを作ってあとで対処し、今はFPGAブロックを動作させるということをゴールにして最短で進みましょう。」 | WS286 の到達目標を「JTAG で起動し PL の block を zedBSD から動かす」に絞った（冒頭、§8）。USB は外し、LS/FS の split は Q1 の WS287。SMP・L2・SD・LAN・SD の起動は PL に要らないので後の WS（Q1 が作る、U8） |
| D5 | 「この計画で取り組みましょう。Fableでの設計やレビューは不要です。…QEMUも、少なくともarmv7 virtとしては積極的に利用しましょう。zynqゲストが利用できるならぜひ利用したいですが、…」（冒頭に全文） | virt を HAL の立ち上げに（§7.2）、host の QEMU 10.0 に有る `xilinx-zynq-a9` を BSP の早い確かめに（§7.3、build 不要） |

Q1 の伝えた事実（2026-10-23）: host に openocd 0.12.0・openFPGALoader 0.13.1。`sudo openFPGALoader -c digilent --detect` の chain: index 0 Cortex-A9 DAP（0x4ba00477）、index 1 xc7z010（0x3722093）。`/dev/ttyUSB0` = JTAG（FT2232H A）、`/dev/ttyUSB1` = UART（B、115200）。ZYBO は USB 給電、boot mode の jumper は JTAG。→ §0・§4.3 に入れた（PL の IDCODE が upstream の cfg に無い事は P1 が読んで見つけた）。

版 1 の §12 の「未確認」の解消: (a) SPL の `spl_boot_device` は JTAG で `BOOT_DEVICE_RAM`（確認、§4.2）、(b) SPL の text base は `0x0`（確認）、(c) OpenOCD の reset の後に OCM が低位に居る事は p002 の probe（未確認のまま）、(d) `r2` は OpenOCD の `reg r2` で設定（§4.1 の 6）。
