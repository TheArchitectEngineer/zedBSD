<!-- awesome-plan project=zedbsd record=ws289-p001-design -->

# WS289 の設計: ZYBO の単独起動 — SD（SDHCI）・LAN（GEM）・SD からの起動

2026-10-23 P1（[ws289-p001](phase001/phase.md)、worktree p1-ws289-p001、基点 main a2339786d）。親: [WS289](ws.md)。
**版 2**（2026-10-23 P1、同じ worktree）: design-reviewer の指摘（Critical 1・High 1・Medium 7・Low 15、対応の表は §15）に対応した。主な変更: SD の kernel の image を legacy の uImage から **FIT** に（C1: legacy は header を load の 64 byte 下に置き、`0x000FFFC0` = 高位 OCM の alias に書いてしまう）、SD と ramdisk が両方有る時の boot device（H1: initrd が有れば ramdisk が 0x80）、legacy autoroot では zybo に `/boot/boot0` が無い（M1）、RXUBR の mask（M2）、RP1 の glue の順と MDIO の回数（M3）、QEMU の再読みの試験（M4）、Pi 5 の SD の確認（M5）、CSD の守り（M6）、`rpi4-sdio-host.c` の範囲（M7、U7）、v2 の divisor（L1）、card detect の扱いの統一（L2）、`net.conf` は必須でない（L3）、WP = MIO55（L4）、gpio0 46 の訂正の範囲（L5）、delay の bit（L6）、出典の行（L7）、R5 の格下げ（L8）、ISR は read で消える（L9）、rpi5 の `mii-phy.c`（L10）、host 試験の直し（L11）、kernel の大きさの検査（L12）、FAT16（L13）、option の確かめ（L14）、p003 の LW（L15）。前提: [WS286](../ws286/ws.md) の [design.md](../ws286/design.md) 版 3（板の事実 §1、JTAG と SPL §4、SD・LAN の素描 §5.3・§5.4、memory §3.12）と [phase005](../ws286/phase005/phase.md)（zybo の BSP は main）。

**到達目標**（[ws.md](ws.md)、2026-10-23 ユーザー「メモリがあまり多くないのでSDドライバがあるとうれしいです。LANのドライバもついでに動かしてしまえませんか？単独ブートできるといいと思います。」）: 実機の ZYBO を SD の boot mode にして電源を入れると、JTAG 無しで起動し、root は SD の partition（ramdisk の 512 MiB を使わない）、LAN で DHCP を取り SSH で入れ、SSH で kernel を差し替えて `reboot` できる。

**設計の決定（2026-10-23 ユーザー、Q1 経由、原文）**:「rpi4を参照するよりも、共通部分は分離しましょう。」→ ZYBO の code から Pi の driver を写したり参照したりしない。共通部分を汎用の driver に分ける: **汎用の SDHCI の core**（`src/drivers/mmc/sdhci.c`）を rpi4-sdhci と Zynq の SDIO0 の glue の両方が使い、**汎用の Cadence GEM の core**（`src/drivers/ethernet/cadence-gem.c`）を rp1-ethernet（64 bit の descriptor）と Zynq の GEM0 の glue（32 bit）の両方が使う。分離は ZYBO の glue より前の独立した Phase（p002・p003）にし、Pi 4 の SD と Pi 5 の Ethernet の回帰の無い事を確かめる（build の warning 0 と、実機か T1 の確認。どちらかは §9 に書く）。platform に固有の物（clock・pin・PHY・癖）は薄い glue に置く。§13 に原文と扱い。

## 0. 出典と確認の水準

- 読んだ tree の物（全部 main a2339786d）: `src/drivers/platform/rpi4/rpi4-sdhci.c`（全 1285 行）・`.h`、`src/drivers/platform/rpi5/rp1-ethernet.c`（全 1790 行）・`.h`、`src/drivers/ethernet/mii-phy.c`・`include/drivers/ethernet/mii-phy.h`・`bcm54213pe.h`、`include/drivers/mmc/dw-mmc.h`・`src/drivers/mmc/dw-mmc.c`（関数の一覧と CSD の復号）、`include/drivers/generic/dma.h`・`src/drivers/generic/dma.c`（coherent の割り当て、`drv_dma_map` の範囲、`kern_pmem_map_uncached` の weak）、`include/hal/hal.h`（`hal_pmem_map_uncached`、`hal_dcache_*`、`hal_rtc_read_counter`）、`src/hal/armv7/`（一覧、`cache.c` の関数、`clock.c` の `hal_rtc_read_counter`、`bootinfo.c` の handoff と bootargs、`bsp-zybo/zybo.h`・`slcr.c`・`board.c` の reset）、`src/kern/dcache.c`・`include/kern/dcache.h`、`src/kern/platform/rpi4.c`・`riscv-virt.c`・`visionfive2.c`（boot device の公開）・`README.md`、`src/kern/vfs.c`（root の選び方: legacy autoroot・`rootpart=`・loader の boot partition の FAT）、`src/kern/boot.c`（parameter の名前）、`src/kern/partition.c`（名前の付け方）、`include/kern/boot.h`・`platform.h`・`net/net-device.h`・`irq.h`・`device-io.h`・`pmem.h`、`docs/reference/kernel-boot-parameters.md`、`platform/armv7/*.mk`・`Makefile`、`platform/riscv64/disk-image.mk`・`rootfs.mk`・`tools/make-riscv64-disk-image.py`・`make-riscv64-root-image.py`、`tools/build/`（`ufs_format.py`）、`platform/arm64/vmunix.mk`（rpi4-sdhci の配線と host 試験の作り方）・`firmware.mk`・`rpi4-boot-firmware.notice`、`userland/firmware/zedbsd-firmware.mk`、`tests/rpi4-sdhci-host-test.c`（先頭と構成）、`config/ci/config-zybo.mk`・`config-rpi4.mk`・`config-rpi5.mk`、`userland/base/etc/rc.conf`・`net.conf`、`userland/base/init/services/networking`・`networkd`、`userland/base/networkd/lan-configure.c`・`net/reconcile.c`（net.conf に無い interface は down）、`userland/packages/network/openssh/`（Makefile、`files/sshd.service`・`sshd-start`）、`plan/tools/zybo/*`（spl-build.sh・qemu-zynq.sh・zybo-common.sh・jtag-boot.sh）、`plan/tools/guest/net.conf`、`plan/tools/boot-test.sh`（`raspi4b` の mode）、`plan/agents/protocol.md`（T1）、`.claude/agents/design-reviewer.md`。
- 外の source は scratchpad に取り（tree に入れない）、下の版で読んだ。comment に引く時の形は Guardrail の「Value(s) referenced:」「Procedure referenced:」（block 単位）。

| 出典 | 版 | 読んだ file（SHA-256 の先頭 16 桁） |
| --- | --- | --- |
| QEMU（GPL-2.0、値と振る舞いだけ） | v10.0.11（host の Debian と同じ tag、WS286 p001 が取った物） | `hw/arm/xilinx_zynq.c`（cab3322dad698895）、`hw/sd/sdhci.c`（b7986d834513e17b）、`hw/net/cadence_gem.c`（9620f7d828e32345）、`hw/misc/zynq_slcr.c`（9ff65ce3dc0aee2f） |
| Linux（GPL-2.0、値と手順だけ） | v6.12 | `drivers/mmc/host/sdhci-of-arasan.c`（320a356f586e841c）、`drivers/net/ethernet/cadence/macb_main.c`（38508b69e04345af）、`drivers/net/phy/realtek.c`（0cb29edc739e7eff）、`arch/arm/boot/dts/xilinx/zynq-7000.dtsi`（71b9e572a111e037）、`zynq-zybo.dts`（462b47ab318dc279）、`drivers/clk/zynq/clkc.c`（8c6d138910712ce2） |
| U-Boot（GPL-2.0+。SPL は tree の外で build する物。zedBSD の code には写さない） | v2025.07（raw.githubusercontent.com、`refs-ws289/`） | `arch/arm/mach-zynq/spl.c`（5501d11d146c1e3e）、`arch/arm/mach-zynq/Kconfig`、`common/spl/Kconfig`（48cfdc45e8452ad9）、`common/spl/spl.c`（419ff14906623633）、`common/spl/spl_fat.c`（d77937c3a85bb009）、`common/spl/spl_mmc.c`（381746c80b350a50）、`common/spl/spl_legacy.c`（677d4a9e4c8db829）、`arch/arm/lib/spl.c`（b83929ef251836aa）、`include/spl.h`（3b3baab989346eb1）、`include/system-constants.h`（eacc35d0c4d3fd1b）、`scripts/Makefile.xpl`（c21c106e425f1efd）、`tools/zynqimage.c`（a8c8f16e32364612）、`doc/develop/falcon.rst`（a39154a30376568e）、`configs/xilinx_zynq_virt_defconfig`（502c9bd39d0f5e3a）、`include/configs/zynq-common.h`（688bc1b6f781a565）、`arch/arm/dts/zynq-zybo.dts`（739e1d677a95a2fd）、`board/xilinx/zynq/board.c`（6d567b0bc50eb4b9） |

- 確認の水準: build・QEMU・実機は何も走らせていない（設計の Phase）。host の道具の有無は `command -v` で確かめた（§5.4）。UG585（TRM）は PDF を取っていない。「UG585」と書く事実は device tree・Linux・U-Boot・QEMU の source に現れる値で確かめた物で、それ以外は **未確認** と明記する。

## 1. 事実（板・SoC・QEMU・tree）

### 1.1 SDIO0（Arasan SDHCI 8.9a）

- register `0xE0100000`（4 KiB）、GIC ID 56（SPI 24）、clock `clk_xin` = SLCR の SDIO clock（`clkc 21`）、`clk_ahb` = cpu_1x（dtsi の `sdhci0`、compatible `arasan,sdhci-8.9a`）。
- Linux の `arasan,sdhci-8.9a` の pdata（`sdhci-of-arasan.c` 1331〜1334 行）: `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN`（capability の base clock は当てにしない）、`SDHCI_QUIRK2_PRESET_VALUE_BROKEN`（preset value を使わない）、`SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN`（divisor 0 = base clock そのままを使わない）、`SDHCI_QUIRK2_STOP_WITH_TC`。SDHCI の版は v2（capabilities は 32 bit の 1 word、divisor は 2 の冪 1〜256）。
- base clock: SLCR `SDIO_CLK_CTRL`（`0xF8000150`: bit 0 CLKACT0、bit 1 CLKACT1、bits 5:4 SRCSEL（0 = IO PLL）、bits 13:8 DIVISOR。Values referenced: Linux `clkc.c`、QEMU `zynq_slcr.c` の clock の計算の注記（bits[13:8] divisor、bits[5:4] mux））。ZYBO の `ps7_init_gpl.c` は DIV=20 → IO PLL 1000 MHz / 20 = **50 MHz**（WS286 §1.2）。AMBA clock は `APER_CLK_CTRL`（`0xF800012C`）の bit 10（SDIO0。Values referenced: Linux `clkc.c` の aper の gate の bit。p004 で読んで確かめる）。
- card detect / write protect: SD0 の CD は MIO47、WP は **MIO55**（版 2、L4: U-Boot `zybo_ps7_init_gpl.c` 3576〜3582 行の `SD0_WP_CD_SEL` の値、WP = 0x37、CD = 0x2F）の設定が SLCR `SD0_WP_CD_SEL`（`0xF8000830`）に有り、`ps7_init_gpl.c` が書く。SDHCI の `PRESENT_STATE`（+0x24）の bit 16（card inserted）がそれを映す（**実機で未確認**。QEMU は `-drive if=sd` が有れば立てる）。
- QEMU `xilinx-zynq-a9`（`xilinx_zynq.c` 316〜341 行）: SDHCI ×2（`0xE0100000` IRQ 56、`0xE0101000` IRQ 79）、`sd-spec-version` = 2、`capareg` = **`0x69ec0080`**（UG585 の値）: base clock の field（bits 15:8）= 0（Linux の quirk と同じ）、timeout clock の unit = MHz で値 0、max block length 512、8 bit（bit 18）、**ADMA2（bit 19）・HS（bit 21）・SDMA（bit 22）・suspend（bit 23）・3.3 V（bit 24）**、**64 bit の system bus（bit 28）= 0** → DMA の address は 32 bit だけ。`-drive if=sd,index=0` が SDIO0 の card。QEMU の `sdhci_write`（1173 行）は 8/16/32 bit の access を shift と mask で受け、SDMA は `blksize` の boundary（bits 14:12、既定 512 KiB）で止まる（604〜655 行）。
- tree の rpi4-sdhci.c（Pi 4 の eMMC2 = iProc の SDHCI）の形: **PIO・poll・割り込み無し**、1 run = CMD18/25 ＋ auto CMD12 で最大 128 block、失敗した run は 1 block ずつ（BUG-295）、CMD13/CMD12 の回復（BUG-288）、**32 bit の access だけ**（iProc の制約。`w16(REG_COMMAND)` は transfer mode と 1 word で書き、`w16(REG_BLOCK_COUNT)` は block size と 1 word で書く）、時計は **arm64 の `cntpct_el0` の inline asm**（450〜474 行）、直接 map は `DIRECT_BASE 0xffff000000000000`（28 行）、`HAL_BOARD_RPI5` の `#if` で base clock 200 MHz（513〜522 行）、`d_block_count = 0xffffffff`（CSD は読まない、228 行）、`d_max_transfer_blocks = 128`、`DISK_REMOVABLE`、`DISK_STATS_KIND_SDMMC`。公開の口は `drv_rpi4_sdhci_init(physical_base)`・`drv_rpi4_sdhci_disk()`（rpi4.c・rpi5.c・Wi-Fi の `rpi4_sd_base` が使う）。host 試験 `tests/rpi4-sdhci-host-test.c`（946 行）は `platform/arm64/vmunix.mk` 415〜429 行が `#include` 行を消し 2 つの asm を `host_counter()`/`host_frequency()` に置換した写しを作って、偽の controller に対して **送った command の列**（CMD18/25 ＋ block count ＋ auto CMD12、1 block は CMD17/24、失敗 → CMD13・CMD12 の回復 → 1 block ずつ）を確かめる。

### 1.2 GEM0（Cadence GEM）と RTL8211E

- register `0xE000B000`（4 KiB）、GIC ID 54（SPI 22）、clock `pclk`/`hclk` = cpu_1x（`clkc 30`）、`tx_clk` = `clkc 13`（SLCR `GEM0_CLK_CTRL`）（dtsi の `gem0`、compatible `xlnx,zynq-gem`・`cdns,gem`）。ZYBO の dts: `phy-mode = "rgmii-id"`、`phy-handle` = `ethernet-phy@0`（**MDIO address 0**、reset の GPIO は **無い**）。
- **訂正**: [ws.md](ws.md) の目的の節の「PHY の reset は gpio0 46」は誤り。Linux と U-Boot の両方の `zynq-zybo.dts` で `reset-gpios = <&gpio0 46 1>` は **`usb_phy0`（USB の ULPI PHY）** の物（Linux 29〜33 行、U-Boot 30〜34 行）。WS286 design §1.1 は既に USB の PHY の物として書いている（版 2、L5: WS286 側の訂正は要らない）。GEM の PHY には dts に reset の GPIO が無い。RTL8211E の reset が板の reset（PS の reset）に繋がっているかは**回路図で未確認**（dts に無い事からの推測）。→ **この WS に Zynq の GPIO の driver は要らない**（USB の WS287 の時に要る）。PHY は MDIO の soft reset（`drv_mii_phy_reset`、BMCR bit 15）で足りる前提で、p005 の実機で PHY が answer しなければ R9。
- Linux の zynq の GEM の config（`macb_main.c` 4910〜4917 行 `zynq_config`）: `MACB_CAPS_GIGABIT_MODE_AVAILABLE | MACB_CAPS_NO_GIGABIT_HALF | MACB_CAPS_NEEDS_RSTONUBR`、burst 16。`NEEDS_RSTONUBR`: 負荷の高い時に DMA が止まり RXUBR（used bit read）の割り込みが続く hardware の問題を、`NCR` の RE を落として立て直す事で回復する（1976〜1991 行。Procedure referenced）。tx clock は link の速さで 2.5/25/125 MHz（`macb_set_tx_clk` 522〜545 行）。MDC の divisor は pclk から（`gem_mdc_clk_div` 2692〜2715 行: ≤ 120 MHz → DIV48）。RGMII は `USRIO`（+0x0C）の bit 0（`GEM_BIT(RGMII)`、`macb_default_usrio`、4239 行の手順）。
- SLCR: `GEM0_CLK_CTRL`（`0xF8000140`: bit 0 CLKACT、bits 5:4 SRCSEL（0 = IO PLL）、bits 13:8 DIVISOR0、bits 25:20 DIVISOR1）、`GEM0_RCLK_CTRL`（`0xF8000138`: bit 0 CLKACT、bit 4 SRCSEL（0 = MIO の RX clock））、`APER_CLK_CTRL` の bit 6（GEM0）。IO PLL 1000 MHz から: **1000BASE-T = DIV0 8・DIV1 1（125 MHz）、100 = DIV0 40・DIV1 1（25 MHz）、10 = DIV0 20・DIV1 20（2.5 MHz）**（WS286 §1.2 の表の値。Values referenced: U-Boot `ps7_init_gpl.c` と Linux `clkc.c`）。SLCR は書く前に unlock（`0x008` = 0xDF0D）、終えたら lock（`0x004` = 0x767B）。
- MAC address: U-Boot が無いので DTB に `local-mac-address` は無い。BootROM・SPL は GEM の filter を書かない。→ §4.6。
- QEMU `xilinx-zynq-a9`（`xilinx_zynq.c` 118〜129・314〜315 行）: GEM ×2（`0xE000B000` IRQ 54、`0xE000C000` IRQ 77）、`-nic`/`-netdev` の 1 つ目が GEM0、**PHY の address は 7**（`xilinx_zynq.c` 125 行の `gem_init` が `phy-addr` = 7 を設定する。`cadence_gem.c` 1807 行は property の既定 0。版 2、L7）、PHY の ID は `0x0141`/`0x0CC2`（1441〜1442 行、Marvell の物）、違う address の MDIO の read は 0xffff（1543〜1551 行）。`ISR` は read で消える（`cadence_gem.c` 603・615 行。Zynq の GEM も `DESCONF` の IRQCOR の bit で clear-on-read。版 2、L9）: `ISR = 0xffffffff` の書き込みは Zynq では効かないが害も無い（rp1 の「read して同じ値を write」の形は両方で動く）。`MID` = `0x00020118`（423 行）= version 2 < 7 → CFG6/CFG8・priority queue は無い（rp1-ethernet.c 779 行の分岐で queue 0 だけになる）。descriptor は `DMACFG` の `DMA_ADDR_BUS_WIDTH` が立つ時だけ 64 bit（424〜432 行）。SLCR の reset の値（`zynq_slcr.c` 348〜354 行）: `APER_CLK_CTRL` = 0x01FFCCCD（GEM0・SDIO0 とも on）、`GEM0_CLK_CTRL` = 0x00003C01、`SDIO_CLK_CTRL` = 0x00001E03。SLCR は lock されていると書けない（559〜561 行）。QEMU の GEM・SDHCI の model は SLCR の clock を見ない。
- tree の rp1-ethernet.c（Pi 5）の形: Cadence GEM の **64 bit の descriptor**（4 word、`DMACR` の DMA64 と `RXQBASEHI`/`TXQBASEHI`）、ring 256、slot 2 KiB、受信は slot から `packet_buf` へ写し、送信は slot へ写す、割り込み（RXDONE を worker が流すまで mask、TXDONE、RXOVR、HRESP で停止）＋ 20 ms の poll の thread（link の確認と取りこぼし）、MDIO は clause 22、PHY は BCM54213PE（`drv_bcm54213pe`）、DMA は RP1 の窓（`drv_rp1_dma_window` → `drv_dma_device_create_window`、coherent）、register の offset と bit は OpenBSD `if_cad.c` を Values referenced。RP1 に固有: device tree の探索（`drv_rp1_find_child`）、clock（`enable_clocks`・`clock_rate`）、PHY の reset の GPIO（`drv_rp1_gpio_*`）、割り込みの経路（`drv_rp1_irq_*`）、MAC の読み（tree か filter）。

### 1.3 Zynq の DMA と cache、tree の DMA の口

- Zynq の PS の DMA master（GEM・SDIO）は CPU の cache と **coherent でない**（ACP は PL 用。WS286 §3.12）。tree の `drv_dma_alloc_coherent` は `constraints.coherent = 0` の device に `kern_pmem_map_uncached`（armv7 は `src/kern/uncached.c` → `hal_pmem_map_uncached`、WS286 p005 で実装、`0xD0000000` の窓）で uncached の view を作る（dma.c 994〜1016 行）。**`drv_dma_map`（streaming）はその device の coherent の割り当ての中の範囲しか map しない**（dma.c 579〜660 行: 他は `ENOTSUP`）→ 任意の kernel の buffer（bio の `b_data`）を直接 DMA する道は tree に無い。cache の操作の口は `kern_dcache_clean_range`/`kern_dcache_invalidate_range`（`src/kern/dcache.c` → `hal_dcache_*`、armv7 の `cache.c` に有り。L2 は off なので L1 だけ）。
- 結論（§3.5・§4.5）: GEM は rp1 と同じ「uncached の slot ＋ 写し」で良い（coherent=0 にするだけ）。SDHCI は PIO（cache に触れない）。SDMA を後で足す時は uncached の bounce（64 KiB）＋ 写し（`drv_dma_map` の制約の中）。

### 1.4 kernel の root の選び方と boot の handoff（armv7）

- `src/hal/armv7/bootinfo.c` 86〜88 行: handoff は `boot_bios_id = 0x80`、`boot_partition_scheme = MBR`、`boot_partition_index = 1`（riscv64 と同じ）。bootargs は DTB の `/chosen/bootargs` から（226〜250 行）。
- `src/kern/vfs.c`: parameter が root を名指さない時は **legacy autoroot**（boot_physical = `kern_platform_block_device` が返す disk、その兄弟の partition から `/etc/zedbsd-root` の marker を持つ UFS を 1 つ探す。無ければ boot partition そのもの）。`rootpart=/dev/mmcblk0p2` が有れば **native root**（`vfs_resolve_native_root` → `block_identity_resolve`、partition でなければ `EINVAL`）。native root の時、loader の boot partition（MBR の index 1 = FAT）は slot `boot0` として全 platform で mount され `/boot/boot0` に出る（vfs.c の native の道、`vfs_publish_boot_filesystems`）。**legacy autoroot の時に loader の FAT を mount するのは Pi だけ**（vfs.c 485 行 `#if defined(HAL_BOARD_RPI4) || defined(HAL_BOARD_RPI5)` の枝。版 2、M1）→ zybo で `/boot/boot0` が要るなら `rootpart=` が必須。native の道では loader の FAT が無い・FAT16/32 でないと VFS の初期化が失敗する（boot.c の boot slot の mount、§7）。partition の disk の名前は親が数字で終わると `p` を挟む（partition.c 612 行）→ `mmcblk0p1`・`mmcblk0p2`。
- `kern_boot_device` の公開は rpi4.c の形（`partition_set_scheme(&drv_partition_scheme_mbr)`、`disk_registry_reset()`、driver の init、device `KERN_DEV_SD`・`bios_id 0x80`・`KERN_DEV_BOOT_ORIGIN`）。
- root の image の道具: `tools/build/ufs_format.py`（host の Python の UFS）、`platform/riscv64/tools/make-riscv64-root-image.py`（MBR: p1 FAT ＋ p2 UFS、`--raw` で UFS だけ、`/etc/zedbsd-root` の marker）、`make-riscv64-disk-image.py`（GPT、`mformat`/`mcopy` で ESP）。host に `mformat`・`mcopy`・`dtc`・`arm-linux-gnueabihf-gcc`・`python3`・`qemu-system-arm` は有る。**`mkimage`・`sfdisk`・`mkfs.vfat`・`bootgen` は無い**（§5.4 で要らない形にする）。

### 1.5 userland の network と SSH

- `networkd`（daemon）と `networking`（`/sbin/net startup`）は `userland/base/init/services/` に有り `rc.conf` で有効。版 2（L3）: `net/reconcile.c` 122 行は「前の設定に有って新しい設定に無い interface」を down にするだけで、**`net.conf` に名の無い有線の interface は networkd が既定で DHCP にする**（`networkd/managed-lan.c` 1009〜1010 行の注記、2026-09-29 のユーザーの決定）→ ZYBO の root に `en0` の `net.conf` は**必須でない**。base の `net.conf`（`lo0` と `dns: mode: dhcp`）のままでよい。timeout を変えたい時だけ `en0` を書く（§4.7）。
- SSH: `userland/packages/network/openssh`（10.5p1、`sshd.service`・`sshd-start` が host key を作る）。Pi 4・Pi 5 の config は `ZEDBSD_USER_PROGRAMS` に `zlib curl openssh ca-certificates openssl` を持つ。**armv7 ではまだ package を build していない**（WS286 p007 は libc・rtld の runtime まで）→ §9 の依存（Q1）。

## 2. 全体の形と決めた事

| # | 問い | 決定 | 根拠 |
| --- | --- | --- | --- |
| D1 | SDHCI の共通化 | **汎用の core `src/drivers/mmc/sdhci.c` ＋ `include/drivers/mmc/sdhci.h`** を新設し、rpi4-sdhci.c は glue（Pi 4/5 の base と clock、公開の口は今のまま）、Zynq は glue `src/drivers/platform/zybo/zynq-sdhci.c`。p002 で分離、p004 で Zynq | ユーザーの決定（冒頭）。core は rpi4 の proven な PIO の code を「関数の移動」で作り、命令の列を host 試験で同一に保つ（§3.7） |
| D2 | GEM の共通化 | **汎用の core `src/drivers/ethernet/cadence-gem.c` ＋ `include/drivers/ethernet/cadence-gem.h`** を新設し、rp1-ethernet.c は glue、Zynq は glue `src/drivers/platform/zybo/zynq-ethernet.c`。descriptor の幅は config（32/64）。p003 で分離、p005 で Zynq | 同上。RP1 と Zynq の差は descriptor の幅・DMA の窓・clock・PHY・割り込みの経路・MAC の出所だけで、ring・MDIO・open/close/transmit/poll・割り込みの処理は同じ |
| D3 | SD の転送 | **PIO・poll**（rpi4 と同じ）。SDMA は Future Work（§3.5 に設計を残す。p004 で `dd` の速さを測り、2 MB/s 未満なら U3 で判断） | ユーザーの動機は RAM で速さでない。PIO は cache の一貫性の問題が無い。`drv_dma_map` が任意の buffer を map しない（§1.3）ので DMA は bounce になり、写しの分 PIO との差が縮む |
| D4 | root | **SD の partition 2 の UFS を `rootpart=/dev/mmcblk0p2` で名指す**（DTB の bootargs）。ramdisk は JTAG の開発の流れに残す | legacy autoroot（marker）でも動くが、名指す方が JTAG の ramdisk と並んだ時に曖昧でない。FAT の p1 は `boot0` として `/boot/boot0` に mount され kernel の更新に使える（§1.4） |
| D5 | BOOT.BIN | **U-Boot SPL の Falcon mode**（SPL が FAT の **`vmunix.itb`（FIT、kernel だけ）** と `system.dtb` を読んで kernel に直接飛ぶ。U-Boot 本体は入れない。版 2: legacy の uImage は header を load の 64 byte 下に置くので使えない、§5.1 C1） | §5.1。defconfig に `SPL_OS_BOOT=y`・`SPL_LOAD_FIT=y` が既に有り、Zynq の `spl_start_uboot()` は常に 0（Linux を起動）。入口の register の約束（r0=0・r1=0xffffffff・r2=DTB）が JTAG と同じ。U-Boot 本体を足すと `boot.scr`・env・2 段の load と image が増える |
| D6 | SPL の DDR の footprint | SPL の config の fragment で **BSS・stack・malloc を RAM の上端（`0x1F000000` 以上）に移し、DTB の load 先（`SPL_PAYLOAD_ARGS_ADDR`）を JTAG と同じ `0x01F00000` に** | defconfig の SPL は BSS `0x100000`〜、stack `0x200000`、malloc 32 MiB をその上に置く（§5.2）: kernel の `0x00100000` と DTB の `0x01F00000` に重なり、FAT から読んでいる最中に SPL 自身を壊す |
| D7 | QEMU | `qemu-zynq.sh` に `--sd IMAGE`（`-drive if=sd`）と `--net PORT`（`-nic user,model=cadence_gem,hostfwd=tcp:127.0.0.1:PORT-:22`）を足す。SD からの起動の鎖（BootROM → SPL）は QEMU では確かめられない（§6） | QEMU zynq に BootROM は無く、OCM は `0xFFFC0000` にしか無い（`xilinx_zynq.c` 237〜239 行）ので `0x0` の SPL を走らせられない |
| D8 | 再起動 | `reboot` → `hal_reset`（SLCR `PSS_RST_CTRL`、WS286 p005 で実装済み）。BootROM が SD から読み直す。SwitchBot は使わない（未接続） | boot mode の pin は POR で latch され soft reset で保たれる見込み（**未確認**、p006 で `reboot` を 2 回続けて確かめる）。詰まった時は JTAG（`jtag-reset.sh`、SD の mode でも効く） |

## 3. SDHCI

### 3.1 責務と境界

- **core `src/drivers/mmc/sdhci.c`**（Zlib、新規。中身は rpi4-sdhci.c の関数の移動）: SDHCI v2 の register の操作、card の初期化（CMD0・8・ACMD41・2・3・9・7・16・ACMD6）、clock の分周、PIO の run（CMD17/18/24/25 ＋ auto CMD12）、回復（BUG-288・BUG-295 の道）、`struct disk` の公開（`mmcblk<unit>`）、`bio` の処理。**board の事を知らない**: register の仮想 address、base clock、quirk、unit 番号、card detect は config で受ける。
- **glue**（board ごと）: register の map、clock と pin と reset の準備、base clock の決定、config を埋めて `drv_sdhci_attach` を呼ぶ、platform の binding に disk を返す。rpi4 の glue は `rpi4-sdhci.c` に残す（名前と公開の口は変えない。rpi4.c・rpi5.c・rpi4-wifi.c は触らない）。Zynq の glue は `src/drivers/platform/zybo/zynq-sdhci.c`。
- dw-mmc.c（DesignWare、VF2）は別の controller なので触らない。`drv_dw_mmc_config` の形（registers・clock_rate・card_present・unit）に揃える。

### 3.2 公開の interface（`include/drivers/mmc/sdhci.h`、新規）

```c
/* How many controllers the core serves at once (Pi 4: one; Zynq: SDIO0). */
#define DRV_SDHCI_UNIT_MAX	2U

/* Quirks a board's glue declares; none yet (version 2 dropped the divisor flag, see below). */

struct drv_sdhci_config {
	volatile uint8_t *registers;	/* mapped as device memory by the glue */
	uint64_t base_clock_hz;		/* 0: take the capabilities' base clock (100 MHz when that is 0 too, as today) */
	uint64_t max_clock_hz;		/* 0: 25 MHz (default speed, as today) */
	unsigned quirks;
	int (*card_present)(void *context, int *present);	/* NULL: a card is taken to be in */
	void *context;
	unsigned unit;			/* names mmcblk<unit> */
};

/* What the core found, for the boot log and the tests (CSD from p004 on). */
struct drv_sdhci_card {
	uint32_t capabilities;
	uint32_t rca;
	int high_capacity;
	uint64_t block_count;		/* 0xffffffff until the CSD is decoded (p004) */
	unsigned bus_width;
	uint64_t card_clock;
};

int drv_sdhci_attach(const struct drv_sdhci_config *config, struct disk **disk);
int drv_sdhci_card_info(unsigned unit, struct drv_sdhci_card *card);
```

- 内部の状態 `struct sdhci_unit`（今の `struct rpi4_sd_unit` ＋ config の写し）は core の static な配列 `units[DRV_SDHCI_UNIT_MAX]`。今の file static な `unit`・`last_error_status` を unit の中に入れる（`last_error_status` も unit ごと）。全部の内部関数は `unit` を引数に取る形に直す（今は global を見る）。
- register の access: **32 bit の access だけの今の道を core の唯一の道にする**（`r8/r16/w8/w16` は word の read-modify-write、`REG_COMMAND` は transfer mode と、`REG_BLOCK_COUNT` は block size と 1 word で書く）。Arasan は 8/16 bit の access も受けるが、32 bit の paired write は SDHCI の仕様の範囲で Arasan でも正しい（command と transfer mode は同じ word、block size と count は同じ word）。code の道を 1 つにして Pi 4 の振る舞いを変えない。quirk にしない。
- 時計: `cntpct_el0` の asm を **`kern_rtc_read_counter(&counter, &frequency)`**（`include/kern/clock.h`。rp1-ethernet.c の `wait_register` が同じ物を init の時点で使っている）に替える。失敗（false）は `EIO`。armv7 では `hal_rtc_read_counter`（global timer、p005）。
- map: `DIRECT_BASE + physical_base` は arm64 だけの物なので glue に残す。rpi4 の glue は今と同じ式で `registers` を作る（`kern_device_map` に替えるのは Pi の振る舞いの変更になるので p002 ではしない。Future Work）。Zynq の glue は `kern_device_map(0xE0100000, 0x1000, KERN_DEVICE_UNCACHED, &mapped)`（vf2-mmc.c 109 行の形）。
- base clock: core は `config.base_clock_hz` が 0 なら capabilities の bits 15:8 × 1 MHz、それも 0 なら 100 MHz（今の `set_clock` の順）。rpi4 の glue が `HAL_BOARD_RPI5` の時 200 MHz を渡す（今の `#if` を glue に移す。Value referenced の注記も移す）。Zynq の glue は SLCR から計算した 50 MHz。
- `max_clock_hz`: 今の `set_clock(25000000U)` を config にするだけ（既定 25 MHz）。高速（CMD6、50 MHz）はしない。
- 版 2（L1）: `DRV_SDHCI_QUIRK_CLOCK_DIVISOR_ZERO_BROKEN` は**落とす**（分岐が無い flag に意味が無い。今の `set_clock` は `divisor = 1` から始めるので divisor 0 を選ばない）。代わりに **SDHCI の版で divisor の上限を分ける**: 今の `set_clock`（rpi4-sdhci.c 517〜540 行）は v3 の 10 bit の符号化（`divisor < 0x400`、上位 2 bit を bits 7:6）で、v2 では N ≤ 128（8 bit）。core は `HOST_VERSION`（+0xFE の下位 byte）を読み、v1/v2 なら N の上限を 128 にする（50 MHz / 256 = 195 kHz で識別の 400 kHz を下回るので、v2 の Zynq では N=64 → 390 kHz）。Pi 4（iProc、v3）は変わらない。`quirks` の field は残す（中身は空。将来の 50 MHz の高速の時に）。
- 版 2（L2、統一）: `card_present` は **参考の情報**: core は attach の最初に呼んで結果を log（`sdhci: card detect says absent`）に出し、**初期化は常に試みる**。初期化が落ちた時に `ENODEV` を返す（§7 の「card 無し」はこの道）。rpi4 の glue は NULL（log 無し、今と同じ）。Zynq の glue は `PRESENT_STATE` の bit 16 を読む関数を渡す（CD の配線が未確認のため、止める根拠にしない）。

### 3.3 rpi4 の glue（p002 の後の `rpi4-sdhci.c`）

```c
int drv_rpi4_sdhci_init(uintptr_t physical_base)
{
	struct drv_sdhci_config config = {0};
	config.registers = (volatile uint8_t *)(DIRECT_BASE + physical_base);
#if defined(HAL_BOARD_RPI5)
	config.base_clock_hz = 200000000U;	/* Value referenced: bcm2712.dtsi clk_emmc2 (moved from the core) */
#endif
	config.unit = 0;
	return drv_sdhci_attach(&config, &rpi4_disk);
}
struct disk *drv_rpi4_sdhci_disk(void) { return rpi4_disk; }
```

- 残る物: `#include`、`DIRECT_BASE`、`drv_rpi4_sdhci_init`・`drv_rpi4_sdhci_disk`、起動の log の 1 行（`sdhci: base=… caps=…` は core が出す）。rpi4.c の「QEMU の legacy controller への fallback」（`drv_rpi4_sdhci_init` を 2 回呼ぶ）は core が unit 0 の attach の失敗の後にもう一度 attach できる事を要る → core は失敗した attach の unit を空に戻す（今の code は `unit.disk = 0` にしてから返している。同じ）。

### 3.4 Zynq の glue `src/drivers/platform/zybo/zynq-sdhci.c`（p004）

```c
int drv_zynq_sdhci_init(struct disk **disk);	/* SDIO0; ENODEV when no card */
```

1. SLCR（`drv_zynq_slcr`、§4.4 の kernel 側の小さな driver を共有）: `APER_CLK_CTRL` の bit 10 と `SDIO_CLK_CTRL` の CLKACT0 を読み、落ちていれば unlock → 立てる → lock（SPL の後は立っている見込み。QEMU の reset 値も立っている）。`SDIO_CLK_CTRL` の SRCSEL と DIVISOR と IO PLL の FDIV（`IO_PLL_CTRL` bits 18:12）から base clock を計算し log に出す（実機 50 MHz。QEMU は 1000/30 ≈ 33 MHz だが model は clock を見ない）。計算の分母が 0 なら 50 MHz と仮定して警告。
2. `kern_device_map(0xE0100000, 0x1000, KERN_DEVICE_UNCACHED, &registers)`。
3. config: `base_clock_hz` = 1 の値、`quirks = 0`、`card_present` = PRESENT_STATE の bit 16 の読み（§3.2 の方針: log だけ）、`unit = 0`。`drv_sdhci_attach`（v2 なので divisor の上限 128、§3.2）。
4. 割り込み 56 は使わない（PIO・poll）。将来 `kern_irq_register(56, …)` で完了の待ちを sleep に替える時のために注記だけ残す。

### 3.5 DMA（この WS では使わない。Future Work の設計）

- Zynq の SDIO は SDMA と ADMA2（32 bit）を持つ（§1.1）。tree の制約（§1.3）から、bio の buffer を直接 DMA せず **uncached の bounce buffer**（`drv_dma_alloc_coherent`、64 KiB = `d_max_transfer_blocks` 128 × 512、`address_bits = 32`、`coherent = 0`）に写す形になる: 書き込みは bio → bounce の `memcpy` → SDMA、読みは SDMA → bounce → bio の `memcpy`。SDMA は `SYSADDR`（+0x00）に bus address、`BLOCK_SIZE` の boundary（bits 14:12 = 7 → 512 KiB）、`TRANSFER_MODE` の DMA enable（bit 0）、完了は `INT_STATUS` の TRANSFER_COMPLETE、boundary で `DMA_INTERRUPT`（bit 3）が来たら `SYSADDR` を書き直す（64 KiB の bounce を 64 KiB 揃えに取れば boundary は来ない）。uncached の bounce なので cache の clean/invalidate は要らない（direct map で触らない約束、hal.h の `hal_pmem_map_uncached` の注記）。代案の「bio の buffer を `kern_dcache_clean_range`/`invalidate_range` して物理 address を渡す」は、bio の buffer が物理に連続である保証と、invalidate が buffer の外の同じ cache line を壊さない（64 byte 揃え）保証が要り、tree の DMA の口の外になるので採らない。
- 効果の見込み: PIO は 1 word ずつ `kern_mmio_read32` で 512 byte/block → 25 MHz・4 bit の 12.5 MB/s の線を CPU が追える（650 MHz の A9）。bounce の DMA は `memcpy` 1 回分（数 GB/s）だけ減るので速さの差は大きくない。CPU の占有は減る。

### 3.6 root を SD に（p004）

- platform の binding `src/kern/platform/zybo.c`（WS286 p006 が作る file。p004 で足す）: `kern_platform_init` は `partition_set_scheme(&drv_partition_scheme_mbr)`、`disk_registry_reset()`、`drv_zynq_sdhci_init(&sd_disk)`。device の公開（版 2、H1）: **loader が initrd を渡した時（`hal_get_arch_handoff("armv7.initrd")` が有る = JTAG・QEMU の ramdisk の流れ）は ramdisk が device 0（`bios_id 0x80`、`KERN_DEV_BOOT_ORIGIN`）で SD は 0x81、initrd が無い時（SD からの起動）は SD が 0x80・BOOT_ORIGIN**。`kern_platform_block_device` は `device_class == KERN_DEV_SD` の `bios_id` で選ぶ。こうしないと、card を差したままの JTAG の ramdisk の試験（WS286 p010・p008 の受け入れ）で root が SD に奪われる（legacy autoroot は boot_physical の兄弟の partition から marker を探すため）。`kern_platform_refresh_devices` は変えない（SD は boot の前に要る）。
- root の指定: SD の DTB の bootargs に **`rootpart=/dev/mmcblk0p2`**（`make sd-image` が必ず書く。host 試験で DTB の bootargs を検査する）。JTAG の ramdisk の流れ（p010）は bootargs に rootpart が無く legacy autoroot（ramdisk が 0x80 なので ramdisk の root）。**JTAG の kernel で SD の root を試す時（p004・p005 の実機）は `jtag-boot.sh --append rootpart=/dev/mmcblk0p2` を付け、`--initrd` は付けない**（initrd が有れば ramdisk が 0x80 だが、rootpart の native の道は bios_id に依らず partition を名指すので、付けても root は SD。`/boot/boot0` は native の道で mount される）。版 2（M1）: 「rootpart を書き忘れても legacy autoroot で同じ結果」は**誤り**だった: zybo の legacy autoroot では loader の FAT（`/boot/boot0`）が mount されず（§1.4）、`deploy.sh` が落ちる。rootpart は必須。
- SD の image（`make sd-image`、`$(BUILD)/zybo-sd.img`、§5.4）: p1 FAT16（64 MiB、bootable、`BOOT.BIN`・`vmunix.itb`・`system.dtb`）、p2 UFS（`root.ufs`、`RISCV64_NATIVE_ROOT_MIB` と同じ 256 MiB の既定、`/etc/zedbsd-root` の marker、tree は `$(BUILD)/rootfs`）。`platform/armv7/rootfs.mk`（WS286 p010 が ramdisk の `root.img` のために作る）に `root.ufs`（`--raw`）の規則を足す。root の中身の違い: sshd と network の program（§4.7）。`/etc/net.conf`・`/etc/rc.conf` は base の物。
- QEMU: `qemu-zynq.sh --sd build/zybo/zybo-sd.img`（`-drive if=sd,index=0,file=…,format=raw`）と `--append 'rootpart=/dev/mmcblk0p2'`。kernel は `-kernel vmunix`（ELF）のまま（BOOT.BIN は QEMU では使わない、§6）。

### 3.7 試験と回帰（SDHCI）

- host: `tests/rpi4-sdhci-host-test.c` を **`tests/sdhci-host-test.c`** に改め、写す file を `src/drivers/mmc/sdhci.c` に、`sed` の置換を「`#include` を消す」だけにし、試験が `kern_rtc_read_counter`（偽の時計）と `kern_mmio_read32/write32` を与える形にする（今の `host_counter/host_frequency` の役）。版 2（L11）: 試験の file の中は直す（今は `unit.high_capacity`・`DIRECT_BASE`・`drv_rpi4_sdhci_init` を直に使う: 197〜205 行 → `units[0]`・`drv_sdhci_attach` に）。**検査の意味（偽 controller に送る command の列と回復の順）は変えない**: p002 の受け入れは「同じ列」= 振る舞いが同じ事の証拠。`platform/arm64/vmunix.mk` 415〜429 行の規則を新しい名前に直す（`CHECK_RUN_TARGETS` の行も）。glue の `HAL_BOARD_RPI5` の 200 MHz の道は host 試験では通らない（build だけ。M5 の実機の確認へ）。
- build: p002 で `config-rpi4.mk`・`config-rpi5.mk`・`config-amd64.mk` の vmunix が warning 0（arm64 は rpi4・rpi5 の両方が sdhci.c を link する。vmunix.mk 51・62 行）。
- QEMU（T1）: `BOOT_MODE=raspi4b plan/tools/boot-test.sh <Pi 4 の image>`（PNG で `login:`。raspi4b は legacy controller の fallback の道を通る）。**これが Pi 4 の SD の回帰の確認**（QEMU の証拠）。
- 実機の Pi 4: この WS では流さない（ユーザーの試験の一覧に「Pi 4 の SD の起動（p002 の後）」を足す事を Q1 に頼む）。**Pi 5 の SD**（同じ glue、base clock 200 MHz、QEMU に raspi5 は無い）: 版 2（M5）: p003 の Pi 5 の実機の kernel は **p002 を含む main** から作り、`sdhci: mmcblk0 ready` と root の mount を p003 の実機の合否に含める（Ethernet と一緒に 1 回で確かめる）。
- p004（Zynq）: host 試験に CSD の復号の case を足す（§3.8）。QEMU zynq（T1、U3 の serial）: `-drive if=sd` の image で `vfs: scan mmcblk0 … 2 entries`、`rootpart selector /dev/mmcblk0p2 resolved`、`login:`、`dd if=/dev/zero of=/root/x bs=1M count=16` と `sync`、`dd if=/dev/mmcblk0p2 of=/dev/null bs=1M count=64` の速さ（log の行を serial で読む）。実機（T1、JTAG で kernel、SD は card だけ）: 同じ物と、`reboot` の後に書いた file が残る事。

### 3.8 CSD と block count（p004 で core に足す）

- 今の `d_block_count = 0xffffffff` は「大きさを知らない」の印で、partition の scan は MBR の表だけを見るので起動には困らない。Zynq の受け入れで `hw.diskstats`・`vfs: scan … blocks=` に本当の大きさを出すため、core に CSD の復号（CMD9 の応答は既に取っている。v1: C_SIZE・C_SIZE_MULT・READ_BL_LEN、v2: C_SIZE × 1024 block。dw-mmc.c の `decode_csd` と同じ bit の位置、Values referenced: SD Physical Layer Simplified Specification の CSD の表）を足し、`d_block_count` に入れる。版 2（M6）の守り: **CSD_STRUCTURE が 0 か 1 の時だけ**値を入れ、他（CSD v3 の SDUC など）や復号の結果が 0 の時は `0xffffffff` のまま（今の振る舞い）。復号を誤って小さい値を入れると partition の範囲の検査で起動が止まり得るため。Pi 4 の振る舞いの変更（大きさが本物になる）なので p004 の受け入れに Pi 4 の QEMU の boot-test を再び入れ（§9）、Pi 4・Pi 5 の実機（eMMC2、SDXC の card）はユーザーの一覧へ。host 試験に v1・v2 の CSD の case を足す。`response` の 128 bit の取り方: 今の `command()` は `RESPONSE0` だけ返す → CMD9 の時だけ `RESPONSE0..3`（+0x10〜+0x1C）を読む `command_long()` を足す（SDHCI の長い応答は 120 bit で CRC を落として詰めて入るので bit の位置は dw-mmc の `response_bits` と 8 bit ずれる。p004 で QEMU の `sd.c` の CSD と照合して確かめる）。

## 4. GEM（Cadence Gigabit Ethernet MAC）

### 4.1 責務と境界

- **core `src/drivers/ethernet/cadence-gem.c`**（Zlib、新規。中身は rp1-ethernet.c の GEM の部分の移動）: ring（受信・送信、各 256）と slot（2 KiB）の割り当て、controller の reset・設定・start/stop、MDIO（clause 22、`drv_mii_bus` の形で glue と PHY の driver に見せる）、`net_device` の ops（open・close・transmit・poll_receive）、割り込みの処理（RXDONE の mask と worker、TXDONE、RXOVR、HRESP の停止、Zynq の RXUBR の立て直し）、20 ms の poll の thread、link の更新（速さ・duplex を `NETCFG` に、glue の `set_tx_clock` を呼ぶ）。**board の事を知らない**: register の address、descriptor の幅、DMA の device、割り込みの登録、PHY の board の初期化、MAC の出所、tx clock は config の callback。
- **glue**: RP1（`rp1-ethernet.c` に残る: device tree の探索、clock の enable、PHY の reset の GPIO、`drv_rp1_irq_*`、DMA の窓、MAC の読み、BCM54213PE の初期化と link の読み）、Zynq（`src/drivers/platform/zybo/zynq-ethernet.c`: SLCR の clock と速さ、`kern_irq_register(54)`、32 bit の DMA device、MAC の決め方、RTL8211E の delay、`USRIO` の RGMII）。
- PHY: `mii-phy.c`（既存）を両方の基礎に。`bcm54213pe.c`（既存、独自の callback の形）は RP1 の glue が adapter で繋ぐ。RTL8211E の固有の部分（RGMII の delay）は新しい小さな `src/drivers/ethernet/rtl8211e.c`。

### 4.2 公開の interface（`include/drivers/ethernet/cadence-gem.h`、新規）

```c
struct drv_cadence_gem;			/* opaque; lives as long as the kernel once attached */

#define DRV_CADENCE_GEM_FLAG_RESTART_RX_ON_USED_BIT	0x1U	/* Zynq: re-enable RX on RXUBR (Linux MACB_CAPS_NEEDS_RSTONUBR) */
#define DRV_CADENCE_GEM_FLAG_USRIO_RGMII		0x2U	/* Zynq: USRIO bit 0 for RGMII */

struct drv_cadence_gem_config {
	volatile uint8_t *registers;
	unsigned descriptor_width;	/* 32 (two words) or 64 (four words, DMACR DMA64, RXQBASEHI/TXQBASEHI) */
	struct drv_dma_device *dma;	/* created by the glue: window, coherency, address bits */
	uint64_t pclk_hz;		/* the MDC clock divides it; 0 falls back to the largest divider */
	unsigned phy_address;		/* the first address asked */
	int phy_scan;			/* nonzero: when no PHY answers there, scan 0..31 and take the first (QEMU: 7) */
	uint8_t mac[6];			/* unicast; the glue decides where it comes from */
	const char *name;		/* "en0" */
	unsigned flags;
	void *context;
	/* Hooks; NULL where a board has nothing to do. */
	int (*phy_setup)(void *context, const struct drv_mii_bus *bus, unsigned phy, uint32_t identity);
	int (*read_link)(void *context, const struct drv_mii_bus *bus, unsigned phy, struct drv_mii_link *link);
	void (*set_tx_clock)(void *context, unsigned speed_mbps);
	int (*establish_irq)(void *context, void (*handler)(void *argument), void *argument);
};

int drv_cadence_gem_attach(const struct drv_cadence_gem_config *config, struct drv_cadence_gem **gem);
void drv_cadence_gem_start_service(struct drv_cadence_gem *gem);	/* the periodic thread, once interrupts are on (refresh) */
```

- `struct drv_cadence_gem` = 今の `struct gem_device` から RP1 の field（`node`、`register_size`、`phy`（bcm54213pe））を外し config の写しを足した物。core の static な配列（2 つ。Zynq は GEM0 だけ、RP1 も 1 つ）。
- descriptor の幅（32 と 64 の両方を core に）: `GEM_DESC_WORDS` を定数でなく `gem->desc_words`（2 か 4）に、`descriptor[GEM_DESC_ADDRESS_HIGH]` の書き込みと `DMACR` の `DMA64`、`RXQBASEHI`/`TXQBASEHI` の書き込みを `descriptor_width == 64` の時だけに（**Zynq の GEM にその register は無い**。`0x4C8`/`0x4D4` への write は Zynq の APB で bus error になり得るので書かない）。spare の descriptor と ring の大きさの式も `desc_words` で。64 bit の時は今の code と 1 bit も変えない（rp1 の glue が `descriptor_width = 64` を渡す）。
- `read_configuration`（MID・CFG6・CFG8・queue）と priority queue の閉じ方（`GEM_RXQ1BASE`…）は core のまま（version ≥ 7 の時だけ触る。Zynq は version 2 で queue 0 だけ）。MDC の divisor は `pclk_hz` から今の表で。版 2（M3）: **`pclk_hz` は glue が必ず決める**（RP1 の glue は `clock_rate(RP1_CLK_SYS)` が 0 なら今と同じ 200 MHz の fallback を自分で入れる。core は 0 を `EINVAL` にする。「最大の divider に落とす」はしない）。
- `initialize_rings`・`gem_transmit`・`receive_packets` の descriptor の word: `desc_words`（2 か 4）で stride を取り、`ADDRESS_HIGH`（word 2）と **word 3（今の `descriptor[3] = 0` の 4 か所、rp1-ethernet.c 915〜958 行）** は `desc_words == 4` の時だけ書く（版 2、M3。32 bit の stride で word 3 を書くと次の descriptor の status を壊す）。
- 割り込み: core の `gem_interrupt(void *argument)` を glue の `establish_irq` に渡す。RP1 は `drv_rp1_irq_of` + `drv_rp1_irq_establish`（今の code を glue に）、Zynq は `kern_irq_register(54, handler_adapter, gem)`（`kern_irq_handler_t` は `(int irq, kern_irq_ack_t ack, void *argument)` なので glue の adapter が `kern_irq_send_eoi(ack)` を行う。level の SPI、GIC の mode は既定）。
- RXUBR（Zynq の flag）: `GEM_INTERRUPTS` に `IXR_RXUBR`（bit 2）を足し、割り込みで立っていたら `netctl & ~RXEN` を書いて `wmb` の後 `netctl | RXEN` を書く（Procedure referenced: Linux `macb_main.c` の RXUBR の枝）。版 2（M2）: flag の有る時は **RXUBR も RXDONE と同じ mask・unmask の対象**（割り込みの `GEM_IDR` に `RXDONE | RXUBR`、`gem_poll` の `GEM_IER` も同じ。Linux の `rx_intr_mask` と同じ）: そうしないと ring が満ちた時に着信ごとに RXUBR と RXEN の toggle が起きる。RP1 は flag 無しで今のまま。
- PHY の探索と board の初期化の順（版 2、M3: RP1 の MDIO の列と GPIO の順を今のままに保つ）: core は `reset_controller`（MDIO を on）の直後に **`phy_setup(context, bus, phy_address, identity)`** を呼ぶ。`identity` は `phy_scan` の時だけ core が読む（`drv_mii_phy_read_identity` を `phy_address` に、0xffffffff/0 なら 0..31 を走査して最初に答える address。QEMU の 7 のため）。`phy_scan = 0`（RP1）の時は core は MDIO を読まず `identity = 0` で `phy_setup` を呼び、glue が今の順（GPIO の reset の pulse → `drv_bcm54213pe_initialize` が ID を読む）をそのまま行う → RP1 の MDIO の列は今と同じ。Zynq の glue は `identity` で PHY を見分ける（§4.5）。
- link: `read_link` が NULL なら core が `drv_mii_phy_read_link`（速さと duplex）。RP1 の glue は `drv_bcm54213pe_read_link`（速さだけ、full duplex）を包む。core は `NETCFG` の FDEN を `link.full_duplex` で、100/1000 の bit を速さで書き、速さが変われば `set_tx_clock(speed)` を呼ぶ（Zynq の SLCR。RP1 は NULL）。

### 4.3 RP1 の glue（p003 の後の `rp1-ethernet.c`）

- 残る物: `describe_controller`・`describe_phy`・`read_mac`・`enable_clocks`・`clock_rate`（0 なら 200 MHz の fallback、版 2 M3）・`reset_phy`、DMA の窓から `drv_dma_device_create_window`、`drv_rp1_irq_init/of/establish` を `establish_irq` の callback に、BCM54213PE の adapter（`drv_bcm54213pe` の `read/write` に `drv_mii_bus` を包む context。**`phy_setup` = `reset_phy`（GPIO の pulse）→ `drv_bcm54213pe_initialize`**（今の 352〜366 行の順: `reset_controller` → `reset_phy` → bcm の初期化、版 2 M3）、`read_link` = `drv_bcm54213pe_read_link` → `drv_mii_link{speed, full_duplex=1}`）、`drv_rp1_ethernet_init`（config を埋めて `drv_cadence_gem_attach`）、`drv_rp1_ethernet_refresh`（`drv_cadence_gem_start_service`）。公開の口（`drv_rp1_ethernet_init/refresh`）は変えないので rpi5.c は触らない。
- `descriptor_width = 64`、`flags = 0`、`phy_scan = 0`（core は MDIO を読まない）、`name = "en0"`。
- 版 2（L10）: rpi5 の source の一覧（`platform/arm64/vmunix.mk` 61〜82 行）に **`src/drivers/ethernet/mii-phy.c` を足す**（core が `drv_mii_phy_*` を使う。rpi4 の一覧には有るか p003 で確かめる）。

### 4.4 Zynq の glue `src/drivers/platform/zybo/zynq-ethernet.c`（p005）と SLCR の kernel 側

- **`src/drivers/platform/zybo/zynq-slcr.c` ＋ `.h`**（新規、小さい。p004 で作り p005 で使う）: `kern_device_map(0xF8000000, 0x1000, UNCACHED)`、`drv_zynq_slcr_read(offset)`、`drv_zynq_slcr_modify(offset, mask, value)`（spinlock の下で unlock → write → lock）、IO PLL の周波数（`IO_PLL_CTRL` の FDIV × 50 MHz（PS_CLK。bypass なら 50 MHz））、cpu_1x（`ARM_PLL_CTRL`・`ARM_CLK_CTRL`・`CLK_621_TRUE` から。HAL の `slcr.c` と同じ計算を kernel 側に持つ。値は Values referenced: Linux `clkc.c`）。HAL の `zybo_slcr_*` とは別の map（HAL は boot・reset、kernel は clock）。1 CPU なので競合は無いが、HAL の `hal_reset` の unlock と重ならないように driver は sequence ごとに lock を戻す。
- glue の手順:
  1. `APER_CLK_CTRL` の bit 6（GEM0）と `GEM0_RCLK_CTRL` の CLKACT を読み、落ちていれば立てる。`GEM0_CLK_CTRL` を「IO PLL、DIV0 8、DIV1 1、CLKACT」= 125 MHz に（link の前の既定。QEMU の reset 値 0x00003C01 は DIV 60 だが model は見ない）。
  2. `kern_device_map(0xE000B000, 0x1000, UNCACHED)`。
  3. DMA: `drv_dma_device_create(&{address_bits = 32, max_segment_size = 256 × 2048, segment_boundary = 0, coherent = 0})`（identity の bus address。uncached の slot は `kern_pmem_map_uncached` → `0xD0000000` の窓）。
  4. MAC（§4.6）。
  5. config: `descriptor_width = 32`、`pclk_hz` = cpu_1x（実機 108.33 MHz → DIV48 ≈ 2.26 MHz）、`phy_address = 0`、`phy_scan = 1`、`flags = RESTART_RX_ON_USED_BIT | USRIO_RGMII`、`phy_setup` = §4.5、`read_link = NULL`（generic）、`set_tx_clock` = `GEM0_CLK_CTRL` の DIV0/DIV1 を 1000 → 8/1、100 → 40/1、10 → 20/20 に（§1.2）、`establish_irq` = `kern_irq_register(54, …)` の adapter。`drv_cadence_gem_attach`。
  6. `kern_platform_refresh_devices` で `drv_cadence_gem_start_service`（rpi5.c の `drv_rp1_ethernet_refresh` と同じ時点）。
- `USRIO`（+0x0C）の bit 0: core が flag で `start_controller` の中で書く（Procedure referenced: Linux `macb_main.c` `macb_init` の usrio、`GEM_BIT(RGMII)` = bit 0）。

### 4.5 RTL8211E（`src/drivers/ethernet/rtl8211e.c` ＋ `include/drivers/ethernet/rtl8211e.h`、新規、p005）

```c
#define DRV_RTL8211E_IDENTITY	0x001cc915U	/* OUI/model/revision as the identity registers read */
int drv_rtl8211e_set_rgmii_delays(const struct drv_mii_bus *bus, unsigned phy, int tx_delay, int rx_delay);
```

- 手順（Procedure referenced: Linux v6.12 `realtek.c` `rtl8211e_config_init`）: page select（register 0x1f）= 7、extension page select（register 0x1e）= 0xa4、register 0x1c の bit 13（force）| **bit 12（TX delay）| bit 11（RX delay）**（版 2、L6: `realtek.c` 51〜53 行の `RTL8211E_TX_DELAY = BIT(12)`・`RTL8211E_RX_DELAY = BIT(11)` の定義に従う。同じ file の comment（612〜615 行）は逆に書いているが、定義の方を取る。rgmii-id では両方立てるので差は無く、R10 の 4 通りの試しの時に効く）を modify、page を 0 に戻す。ZYBO は `rgmii-id` なので両方の delay を PHY に入れる（RTL8211E の TXDLY/RXDLY の pin の strap は Digilent の回路図で未確認なので register で明示する）。
- Zynq の glue の `phy_setup(identity)`: `drv_mii_phy_reset`（BMCR の reset、`mii-phy.c` が 0.5 s 待つ）→ identity が `0x001cc915`（revision の下位 4 bit は mask して比べる）なら `drv_rtl8211e_set_rgmii_delays(bus, phy, 1, 1)`、他（QEMU の 0x01410cc2）なら何もしない → `drv_mii_phy_start_autonegotiation(bus, phy, gigabit = 1)`。link は generic の `drv_mii_phy_read_link`。
- QEMU の PHY は address 7・Marvell の ID・link up を返す（§1.2）→ 走査で見つかり、delay は飛ばし、autoneg が「完了」になる。

### 4.6 MAC address

- 順: (1) boot parameter **`mac0=xx:xx:xx:xx:xx:xx`**（新しい parameter。`src/kern/boot.c` の表と `docs/reference/kernel-boot-parameters.md` に足す。SD の DTB の bootargs に `make sd-image` が `ZYBO_MAC` から書く）、(2) DTB の `/aliases/ethernet0` が指す node の `local-mac-address`（将来 U-Boot を使う時のため。`drv_fdt_*` で読む）、(3) どちらも無ければ **locally administered の固定の値 `02:5a:59:42:4f:30`**（"ZYBO0"。乱数でなく固定: DHCP の lease と SSH の known_hosts が起動ごとに変わらないため）と警告の 1 行。QEMU は (3) で動く（slirp はどの MAC でも lease を出す）。
- 代案（採らない）: GEM の filter に残った値を読む（rp1 の `read_mac` の 2 段目）: ZYBO では BootROM・SPL が書かないので常に空。Zynq の eFuse/OCM に MAC は無い。

### 4.7 userland（p005）

- `/etc/net.conf` は base の物のまま（版 2、L3: 名の無い有線の interface は networkd が既定で DHCP にする、§1.5）。DHCP の timeout を既定（10 s、`reconcile.c` 197 行）から変えたい時だけ `platform/armv7/zybo/etc/net.conf`（`en0`、`dhcp-timeout: 20`）を `rootfs.mk` で写す（p005 で QEMU の slirp と実機の lease の速さを見て決める。任意）。
- `ZEDBSD_USER_PROGRAMS`（`config-zybo.mk`）に `dhcpc networkd ifconfig ping route hostname` と **`zlib openssl openssh ca-certificates`** を足す（`sshd.service` は package が入れる）。root の password と `sshd_config`（`PermitRootLogin`）は Pi 5 の image と同じ扱い（`plan/tools/guest/build-full-image.sh` の中の設定を p005 で読んで合わせる）。
- 大きさ: openssl ＋ openssh ＋ libc で 10〜15 MiB の見込み（未計測）→ SD の UFS 256 MiB に入る。ramdisk（JTAG）には入れない（p010 の最小の root のまま）。

### 4.8 試験と回帰（GEM）

- host: `tests/cadence-gem-host-test.c`（新規、p003。`sdhci-host-test` と同じ「core の写しを include」の形）: 偽の register（`kern_mmio_*` の表）と偽の DMA（`drv_dma_alloc_coherent` の stub）、偽の `net_device`・`packet_buf`・thread・`kern_rtc_read_counter`（版 2、L15: rp1-ethernet.c には host 試験が無いので偽物は全部新しく書く）で (a) 64 bit の config で `reset_controller`・`initialize_rings`・`start_controller`・`gem_transmit` 1 回・`receive_packets` 1 回が書く register の列（offset と値、順）と descriptor の word が今の rp1-ethernet.c と同じ。**期待値は p003 の最初に、今の rp1-ethernet.c を同じ偽物で動かして採り、file に固定する**（MDIO の列は `phy_setup` を glue に移すので core の列には含めない。含めない事を試験の注記に書く）、(b) 32 bit の config で descriptor が 2 word・word 3 に触れない・`DMA64` を書かない・`RXQBASEHI`/`TXQBASEHI` に触れない・RXUBR の flag で `IDR`/`IER` の word に RXUBR が入る（M2）、(c) MDIO の `PHYMNTNC` の word、(d) 走査が 0xffff を飛ばす。
- build: p003 で `config-rpi5.mk` の vmunix が warning 0（`rp1-ethernet.c` は rpi5 だけが link する: vmunix.mk 68 行）。
- **Pi 5 の Ethernet の回帰の確認**: QEMU に RP1 の machine は無いので **実機の Pi 5 だけ**。T1（または Q1）が Pi 5（master.md の「Pi 5 の実機、SSH で kernel を入れ替え」の板）で p003 の kernel を起動し、DHCP の lease と SSH の login（`plan/tools/guest/guest.sh` の経路）を証拠にする。板が空かない間は p003 を「実装済み・T1 待ち」で区切り、p005（Zynq）へ進んでよい（独立）。Pi 5 の確認は WS289 の完了の条件に入れる（p003 を cleared にする条件）。
- p005（Zynq）: QEMU zynq（T1、U3 ＋ U4 の SSH、§6）: `gem: PHY 7 id 01410cc2`（走査）、`en0 link up 1000 Mbps`、`dhcpc` の lease（10.0.2.15）、host から `ssh -p PORT root@127.0.0.1 uname -m` = `armv7`。実機（T1、JTAG の kernel ＋ SD の root、LAN を繋いで）: `gem: PHY 0 id 001cc915`、`link up 1000 Mbps full duplex`（ユーザーの switch による。100 でも可）、DHCP の address（serial で `ifconfig en0`）、host から `ssh root@<address> uname -m`、`scp` で 8 MiB の file を往復し `cksum` が一致。

## 5. SD からの起動（p006）

### 5.1 BootROM → SPL（Falcon）→ kernel

- BootROM（SD の boot mode）は SD の **最初の partition（FAT）** の root の `BOOT.BIN` を読み、その最初の image（`zynqimage` の header、`tools/zynqimage.c` の説明: 0x20 width detection `0xaa995566`、0x24 identifier `0x584c4e58`、encryption none、image の offset と長さ）= SPL を OCM の `0x0` に置いて走らせる（UG585 の BootROM の章は未読。`zynqimage.c` の header の説明と U-Boot の `spl/boot.bin` の規則から）。**`spl/boot.bin` は U-Boot の build が `mkimage -T zynqimage` で作る**（`scripts/Makefile.xpl` 243 行。`mkimage` は U-Boot の build の中の物。host の u-boot-tools は要らない）。
- SPL（`xilinx_zynq_virt_defconfig` ＋ 下の fragment）: `board_init_f` → `ps7_init()`（clock・MIO・DDR。JTAG と同じ）→ `board_init_r` → `spl_boot_device()` = `BOOT_DEVICE_MMC1`（`mach-zynq/spl.c` 62〜63 行、`ZYNQ_BM_SD`）→ `spl_mmc_load_image` → FS の mode（`SYS_MMCSD_FS_BOOT_PARTITION` = **1**、`common/spl/Kconfig` 911〜914 行）→ `spl_mmc_do_fs_boot`（`spl_mmc.c` 197〜260 行）: **`spl_start_uboot()` が 0 なので先に `spl_load_image_fat_os`**（`spl_fat.c` 110〜160 行: `SPL_FS_LOAD_ARGS_NAME` の file を `SPL_PAYLOAD_ARGS_ADDR` にそのまま読み、`SPL_FS_LOAD_KERNEL_NAME`（既定 `uImage`）を `spl_load_image_fat` で読む）、失敗なら `u-boot.img`（無いので hang）。Zynq の `spl_start_uboot()` は **常に 0**（`mach-zynq/spl.c` 77〜83 行）。
- **kernel の image は FIT（`vmunix.itb`、kernel だけ）**（版 2、C1）。版 1 の legacy の uImage は使えない: `spl_legacy.c` 47〜63 行で `SPL_COPY_PAYLOAD_ONLY` の無い時（FAT の道では立たない。`spl_load.h` 68・83 行で立てるのは LZMA と NOR だけ）は「header ごと読む」= `load_addr = ih_load − 64`、`size = data + 64` → `ih_load = 0x00100000` だと header の 64 byte が **`0x000FFFC0`〜`0x000FFFFF` = 高位 OCM の alias（CPU1 の BootROM の loop と `0xFFFFFFF0` が見える所、WS286 §4.8）** に書かれ、CPU1 を起こすか SPL が abort する。FIT なら `spl_fit.c` 261〜267・298〜337 行が data を `load` の address にそのまま置く（読みの staging は `ALIGN(load, ARCH_DMA_MINALIGN)` から上、`memcpy` で `load` へ。`load` より下には触れない）。
  - FIT の中身（ITS、`platform/armv7/tools/make-zybo-sd-image.py` が書き `dtc -I dts -O dtb` で作る。`/incbin/` で `vmunix.bin` を入れる。`mkimage -f` も中では dtc を呼ぶだけで、hash は `SPL_FIT_SIGNATURE` の無い defconfig では要らない（`spl_fit.c` 325 行））: `/images/kernel { type = "kernel"; arch = "arm"; os = "linux"; compression = "none"; load = <0x00100000>; entry = <0x00100000>; data = /incbin/("vmunix.bin"); }`、`/configurations { default = "zybo"; zybo { kernel = "kernel"; }; }`。SPL は `firmware` が無ければ `kernel` を取り（`spl_fit.c` 818〜821 行）、`os` の property から `IH_OS_LINUX`（847 行）。
  - FIT の structure は SPL が **`SPL_LOAD_FIT_ADDRESS` = `0x10000000`** に読んでから data を `load` へ写す（defconfig 35 行。inline の data なので FIT 全体 ≈ kernel の大きさがそこに置かれる。`0x10000000`〜 は kernel・DTB・SPL の footprint のどれとも重ならない。R18）。
  - `os = linux` の時 `spl_fit_append_fdt`（`spl_fit.c` 376〜386・395〜440・852〜860 行）が走る: FIT に `fdt` の node が無いので SPL は **自分の control DTB（`zynq-zybo` の U-Boot の dts）を `ALIGN(load + size, 8)` = kernel の file の直後に写す**。そこは kernel の **BSS**（`_edata`〜`_end`、p005 の実測で `0x1F1C50`〜`0x7AF000` の間）で、`locore.S` が BSS を消すので kernel には見えない。jump の `r2` はこれでなく下の args。代案（FIT に `fdt` の node を `load = <0x01F00000>` で入れる）は data を 2 度持つので採らないが、append が落ちる時の逃げ道として残す。
  - `SPL_LEGACY_IMAGE_FORMAT`（版 1 の fragment）は**要らない**（FIT の道は `SPL_LOAD_FIT=y` で既に有る）。
- jump（`common/spl/spl.c` 763 行で `spl_image.arg = SPL_PAYLOAD_ARGS_ADDR`、794〜799 行: `os == IH_OS_LINUX` → `jump_to_image_linux`。その前に `spl_board_prepare_for_boot()`（846 行）= Zynq の `ps7_post_config()`（level shifter・PL の reset の解除。JTAG の流れでは走らなかった物）と 797 行の `spl_fixup_fdt(SPL_PAYLOAD_ARGS_ADDR)`（`fdt_shrink_to_minimum` = DTB 自身の memreserve を足す、と `arch_fixup_fdt`。版 2（L8）: defconfig 38 行 `# CONFIG_ARCH_FIXUP_FDT_MEMORY is not set` なので memory node は書き換わらない見込み（`bootm-fdt.c` は未読、推測）。memreserve が足されると HAL の `page.c` が DTB の page を解放せず持ち続ける（小さな漏れ、受け入れる））。`arch/arm/lib/spl.c` 62〜75 行: **`r0 = 0`、`r1 = 0xffffffff`（`MACH_TYPE` 無し）、`r2 = args の address`**、`cleanup_before_linux()`（cache・MMU を落とす）→ entry。**JTAG の `jtag-boot.sh` の入口の約束（WS286 §4.1 の 6）と同じ**。CPU は Secure の SVC、割り込みは mask、CPU1 は BootROM の待ちのまま。
- kernel 側に変更は無い: `bootinfo.c` は r2 の DTB を読み（magic の確かめは p005 の `bsp_find_fdt`）、handoff は MBR の index 1 → FAT が `boot0`。

### 5.2 SPL の config の fragment（`plan/tools/zybo/spl-build.sh` に足す。1 つの SPL を JTAG と SD の両方に）

```
CONFIG_DEFAULT_DEVICE_TREE="zynq-zybo"            # p002 のまま
# CONFIG_SPL_RAM_DEVICE is not set                 # p002 のまま（JTAG の mode で RAM へ飛ばない）
# CONFIG_TOOLS_MKEFICAPSULE is not set             # p002 のまま
CONFIG_SPL_FS_LOAD_KERNEL_NAME="vmunix.itb"        # FIT（§5.1、版 2: legacy は使わない）
CONFIG_SPL_FS_LOAD_ARGS_NAME="system.dtb"          # defconfig の値（明示）
CONFIG_SPL_PAYLOAD_ARGS_ADDR=0x01F00000            # DTB を JTAG と同じ所に
CONFIG_SPL_BSS_START_ADDR=0x1F000000               # SPL の DDR の footprint を RAM の上端へ（D6）
CONFIG_SPL_STACK_R_ADDR=0x1FF00000
CONFIG_SPL_HAS_CUSTOM_MALLOC_START=y
CONFIG_SPL_CUSTOM_SYS_MALLOC_ADDR=0x1F100000
CONFIG_SPL_SYS_MALLOC_SIZE=0x00C00000              # 12 MiB（defconfig は 32 MiB）
```

- 根拠（defconfig の値、`configs_xilinx_zynq_virt_defconfig` 11〜15・46 行と `system-constants.h` 27〜31 行）: BSS `0x100000`〜`0x1FFFFF`、stack `0x200000` から下へ、malloc は BSS の終わり `0x200000` から **32 MiB**（`SPL_SYS_MALLOC_SIZE=0x2000000`）= `0x2200000` まで。kernel `0x00100000` と DTB `0x01F00000` はこの中 → FAT から読む間に SPL が自分の heap・BSS を壊す。移した後: BSS `0x1F000000`〜`0x1F0FFFFF`、malloc `0x1F100000`〜`0x1FCFFFFF`、stack `0x1FF00000` から下（≥ `0x1F000000` の 16 MiB の中）。kernel は entry の後にこの領域を普通の RAM として使う（SPL は終わっている）。
- JTAG の流れへの影響: `board_boot_order` の breakpoint（`spl-build.sh` が `nm` で毎回取る）はそのまま。BSS・stack の address が変わるだけで、JTAG の kernel の load は SPL を止めた後なので関係ない。**SPL の binary は 1 つ**（SHA-256 は p002 の `ed936c13…` から変わる。`jtag-hello.sh` を p006 で 1 回流して JTAG の道が生きている事を確かめる）。
- 大きさ: SPL は OCM の 192 KiB（`SPL_MAX_SIZE 0x30000`）。FAT・MMC・legacy の code は defconfig の SPL に既に有る（`u-boot.img` を FAT から読む道）。legacy の header の解析は数百 byte。p006 の最初に `spl-build.sh` の出力の大きさを見る（R6）。
- build の目標を `spl/u-boot-spl.bin` から **`spl/boot.bin`** に（`Makefile.xpl` 243 行の `zynqimage`。U-Boot の `tools/mkimage` も build される）。`spl-build.sh` は `BUILD/out/spl/boot.bin` も出す。版 2（L14）: `merge_config.sh` は依存を満たさない option を黙って落とすので、`spl-build.sh` 56〜59 行の `grep -qx` の確かめを fragment の**全部の行**に広げる（特に `SPL_HAS_CUSTOM_MALLOC_START`・`SPL_CUSTOM_SYS_MALLOC_ADDR`・`SPL_PAYLOAD_ARGS_ADDR`・`SPL_FS_LOAD_KERNEL_NAME`）。

### 5.3 FAT の中身と image の作り方

| file | 中身 | 作る物 |
| --- | --- | --- |
| `BOOT.BIN` | `spl/boot.bin`（GPL-2.0+、tree の外の build） | `spl-build.sh`（ZYBO_BOOT_BIN の path で `make sd-image` に渡す。tree に入れない・配布しない。§12） |
| `vmunix.itb` | FIT（§5.1 の ITS: kernel の node だけ、`load`・`entry` = `0x00100000`、`os = "linux"`、`data = /incbin/("vmunix.bin")`。FIT の形は U-Boot の `doc/usage/fit/` の public な format。hash の node は無し） | `platform/armv7/tools/make-zybo-sd-image.py`（Zlib、新規）が ITS を書いて `dtc` で作る。host の `mkimage` は要らない |
| `system.dtb` | `platform/armv7/zybo/zybo-sd.dts`（Zlib、新規。`zybo-common.sh` の雛形と同じ `memory@0`・`/chosen`。bootargs = `rootpart=/dev/mmcblk0p2 mac0=…`（`ZYBO_MAC` が有れば）＋ `ZYBO_BOOTARGS`） | `dtc`（host に有る）を image の tool が呼ぶ |

- image（`$(BUILD)/zybo-sd.img`、既定 384 MiB）: MBR、p1 = **FAT16**（type `0x06`、bootable `0x80`、64 MiB、`mformat`・`mcopy` で 3 つの file。版 2（L13）: 64 MiB の FAT32 は cluster の数が 65525 未満になり得て FAT16 扱いと食い違うので、最初から FAT16 にする。BootROM が FAT16 を読む事は UG585 で**未確認**（Xilinx の BootROM は FAT16/32 の両方を読むと広く知られているが文書は未読）、p006 の最初の実機の確認）、p2 = UFS（`root.ufs`、`make-riscv64-root-image.py` と同じ type）。tool は `make-riscv64-root-image.py` の MBR と `make-riscv64-disk-image.py` の `mformat`/`mcopy` の部分を手本にした新しい file（共通化はしない: 2 つの tool は GPT と MBR で形が違う）。
- 版 2（L12）: tool は `--elf $(BUILD)/vmunix` も受け、`llvm-readelf -lW` の `PT_LOAD` から **kernel の `_end`（BSS を含む）＋ SPL が append する fdt の余地（64 KiB）が `0x01F00000` より下**、FIT の data の `load` が `0x00100000`、FIT 全体が `0x10000000` から `0x1F000000` に収まる事を確かめる（`jtag-boot.sh` の `zybo_elf_phys_end` と同じ検査。SPL は DTB を先に、kernel を後に読むので、大きな kernel は DTB を上書きする）。
- make: `platform/armv7/disk-image.mk`（新規）の `sd-image`: `$(BUILD)/vmunix $(BUILD)/vmunix.bin $(BUILD)/root.ufs $(ZYBO_BOOT_BIN) platform/armv7/zybo/zybo-sd.dts` から。`ZYBO_BOOT_BIN` が空なら BOOT.BIN 無しの image（QEMU 用。`-kernel` で起動する）。`ZYBO_MAC`・`ZYBO_BOOTARGS` は任意。書き込みは host の `dd`（ユーザー。`lsblk` で device を確かめる手順を README に）。
- 2 つの partition の順を変えない: BootROM は partition 1、SPL も `SYS_MMCSD_FS_BOOT_PARTITION` = 1 を読む。

### 5.4 host の道具

- 有る: `mformat`・`mcopy`（mtools）、`dtc`、`arm-linux-gnueabihf-gcc`、`python3`、`qemu-system-arm`。無い: `mkimage`（U-Boot の build の中の物を使う）、`sfdisk`・`mkfs.vfat`（Python が MBR を書き、mtools が FAT を作る）、`bootgen`（AMD、使わない）。追加の package は要らない。

### 5.5 boot mode の jumper と電源

- ZYBO の boot mode の jumper **JP5** を `JTAG` から `SD` に替えるのは **ユーザー**（Q1 がユーザーの試験の一覧に入れる）。p006 の実機の確認は「card を差し、JP5 = SD、電源（USB）を入れる」から。JTAG の道具は jumper が SD でも `jtag-reset.sh`（DAP → SLCR）と `jtag-boot.sh` の SPL の breakpoint は効く見込みだが、**`jtag-boot.sh` は `BOOT_MODE` が JTAG でなければ load しない**（WS286 §4.1 の 2 の守り）→ SD の mode の間は JTAG で kernel を入れられない。開発の流れ: jumper は SD にしたまま、kernel の更新は SSH（§5.6）、詰まったら jumper を JTAG に戻して `jtag-boot.sh`。
- 電源: USB 給電、SwitchBot のプラグは ZYBO に**未接続**（WS286 ws.md）。電源の入れ直しはユーザー。`reboot` は §5.6。

### 5.6 kernel の更新と再起動（SSH）

- `plan/tools/zybo/deploy.sh KERNEL.bin [--host zybo]`（新規、Zlib）: `make-zybo-sd-image.py --fit-only --elf vmunix` で `vmunix.itb` を作り（大きさの検査も通す、§5.3）、`scp vmunix.itb root@zybo:/boot/boot0/vmunix.itb.new` → `ssh root@zybo 'mv /boot/boot0/vmunix.itb /boot/boot0/vmunix.itb.old && mv /boot/boot0/vmunix.itb.new /boot/boot0/vmunix.itb && sync && reboot'`。DTB も同じ道（`system.dtb`）。`.old` を残す（memory「VF2 board SSH deploy」と同じ）。
- `/boot/boot0` は native root の時に vfs が mount する loader の FAT（§1.4、private FAT、`bootlog` の service が書いている物と同じ）。FAT の `mv` が rename で原子的かは FAT の実装次第 → `.new` に書いて `sync` してから rename、落ちた時は JTAG で入れ直す。
- `reboot` → `kern_platform_reboot` → `hal_reset`（WS286 p005 の `bsp_reset`: `REBOOT_STATUS` の bit を消して `PSS_RST_CTRL` = 1）→ BootROM → SD。boot mode が soft reset の後も SD のままである事（D8）は p006 で `reboot` を 2 回続けて確かめる。戻って来なければ JTAG（`jtag-reset.sh` は SD の mode でも SLCR に書ける）。

## 6. QEMU（xilinx-zynq-a9）と T1

- `qemu-zynq.sh` の追加（p004・p005）: `--sd IMAGE`（`-drive if=sd,index=0,file=IMAGE,format=raw`。`xilinx_zynq.c` 336 行の `drive_get(IF_SD, 0, 0)`）、`--net PORT`（`-nic user,model=cadence_gem,hostfwd=tcp:127.0.0.1:PORT-:22`。1 つ目の NIC が GEM0）、`--mac`。kernel は ELF を `-kernel` のまま（BootROM・SPL は QEMU で走らない、D7）。
- T1 の流し方（U3 の serial の判定 ＋ **U4 の SSH**）: `qemu-zynq.sh build/zybo/vmunix --sd build/zybo/zybo-sd.img --net 2289 --append 'rootpart=/dev/mmcblk0p2' --expect 'login:' --timeout 180` の後、host から `ssh -p 2289 -o StrictHostKeyChecking=no root@127.0.0.1 'uname -m; ifconfig en0; df /'`（`plan/tools/guest/guest.sh` の形）。QEMU は host 全体で同時に 1 つ（T1）。
- QEMU で確かめられる物: SDHCI の driver（command の流れ、PIO、partition、root の mount、書き読み）、GEM の driver（ring、割り込み、MDIO の走査、DHCP、SSH）、userland の sshd。確かめられない物: SLCR の clock（model は見ない）、RTL8211E の delay、RXUBR の癖、**BootROM → BOOT.BIN → Falcon の鎖**、card detect の pin、SD の実際の速さ。これらは実機（T1、JTAG の kernel で p004・p005、SD の mode で p006）。
- QEMU の SDHCI の model の癖: `sd-spec-version` 2、`capareg` の base clock 0（glue の 50 MHz が無いと 100 MHz の fallback → divisor が違うだけで model は気にしない）。`-no-reboot` の時 `reboot` は QEMU を終える（p005 の `ZYBO RESET FAILED` の注記）。

## 7. 資源・並行性・失敗

- 所有と寿命: SDHCI の unit と disk は kernel の寿命（取り外しは扱わない。`DISK_REMOVABLE` は印だけ）。GEM の ring・slot・`net_device` は kernel の寿命（rp1 と同じ: close で返さない）。SLCR の map は kernel の寿命。uncached の slot（1 MiB ＋ ring）は `kern_pmem_map_uncached` の窓から（65536 page の bitmap、p005 の実装）。
- lock: SDHCI は unit の mutex（`LOCK_RANK_DISK`、1 command ずつ）、GEM は `spinlock`（`LOCK_RANK_DEVICE`、割り込みの中でも取る。ring・counter・register）、SLCR は spinlock（unlock〜lock の sequence）。順: GEM の lock の中で SLCR の lock を取る（`set_tx_clock` は `update_link` の中 = GEM の lock の下）→ SLCR の lock は GEM の lock より下の rank。SDHCI は SLCR を attach の時だけ（lock 無しの時点）。1 CPU。
- 割り込みと DMA: GEM は GIC ID 54（level）、`kern_irq_register`。RXDONE は worker が流すまで mask（rp1 と同じ）。DMA は全部 uncached（§1.3）。SDHCI は poll（割り込み 56 は未使用）。
- 失敗と回復: card 無し → `drv_zynq_sdhci_init` が `ENODEV`、ramdisk が有ればそれが boot device、無ければ `VFS initialization failed`（riscv-virt と同じ到達点）。SD の command の失敗 → rpi4 と同じ回復（CMD12・CMD13、再初期化、`failed` で以後 `EIO`）。GEM の HRESP → 停止して log（rp1 と同じ）。RXUBR → RX の立て直し（flag）。PHY が見つからない → `gem: no PHY answered` で attach は失敗、起動は続く（LAN 無し）。MDIO の timeout → link down。DHCP の失敗 → `networkd` の timeout（20 s）、SSH 無しで `login:` は出る。SPL が FAT の file を読めない → SPL の `hang()`（UART に `spl: error reading image vmunix.itb` か `system.dtb`）→ JTAG か card の作り直し。版 2（M1）: native の道（`rootpart=`）で loader の FAT（p1）が無い・FAT16/32 でない → `boot0` の slot の mount が失敗し **VFS の初期化ごと失敗**（boot.c の boot slot の mount）: SD の image は必ず p1 を FAT にし、`check` の host 試験で見る。`reboot` の後に戻らない → `jtag-reset.sh`。SD と ramdisk の両方が有る JTAG の起動 → ramdisk が 0x80（§3.6、H1）。

## 8. 試験の計画

AGENTS.md の方針（細かい修正ごとに回帰を回さない。build の warning 0 と変えた所の短い host 試験。QEMU・実機は Phase の受け入れで T1 に）。QEMU の証拠と実機の証拠は分けて書く。

### 8.1 host（担当）

- `make ZEDBSD_CONFIG=config/ci/config-{rpi4,rpi5,amd64,zybo,armv7-virt}.mk BUILD=build/<phase> -j vmunix`（それぞれの Phase が触る platform、warning 0）。
- `sdhci-host-test`（p002: 今の 946 行の検査と同じ command の列、p004: CSD の case（v1・v2・v3 は `0xffffffff` のまま））、`cadence-gem-host-test`（p003: 64 bit の期待値 = 今の code の register の列、32 bit の形、RXUBR の mask）、`zybo-sd-image-host-test`（p004 で image の tool と一緒に、p006 で BOOT.BIN 入り: `make-zybo-sd-image.py` の出力の MBR の 2 つの entry（p1 FAT16 bootable）、FAT の 3 つの file（`mdir`）、FIT を `dtc -I dtb -O dts` で戻して `load`・`entry`・`os`・data の長さ、**`0x00100000` より下に何も置かない事と kernel の `_end` < `0x01F00000`**（`--elf`）、DTB の bootargs に `rootpart=/dev/mmcblk0p2`、`check-ufs-image.py` で p2）。
- `llvm-nm`/`llvm-readelf`: `cadence-gem.c`・`sdhci.c` が `rp1`・`rpi4`・`zybo` の symbol を参照しない（`nm -u` に `drv_rp1_*`・`drv_rpi4_*`・`drv_zynq_*` が無い）= 境界の確かめ。

### 8.2 QEMU（T1）

| Phase | machine | 流す物と合否 |
| --- | --- | --- |
| p002 | raspi4b（`boot-test.sh`、PNG） | Pi 4 の image が `login:`（SD の回帰。legacy controller の fallback の道） |
| p004 | xilinx-zynq-a9（`qemu-zynq.sh --sd`、serial U3） | `sdhci: mmcblk0 ready`、`vfs: scan mmcblk0 … 2 entries`、`rootpart selector /dev/mmcblk0p2 resolved to /dev/mmcblk0p2`、`login:` → `root` → `df /` が UFS、`dd` 16 MiB 書き ＋ `sync` → **QEMU を終えて同じ image で起動し直してから**（版 2、M4: `CONFIG_BUF_CACHE_KIB := 0` は RAM の 1/8 ≈ 64 MiB の cache なので、同じ起動の中の再読みは cache から返る）`cksum` 一致、読みの速さの行。`--initrd` と `--sd` の両方を付けた起動で root が ramdisk（H1）。raspi4b の boot-test をもう 1 回（CSD の変更の後） |
| p005 | xilinx-zynq-a9（`--sd --net`、serial ＋ SSH U4） | `gem: PHY 7 id 01410cc2`、`en0 link up`、`dhcpc` の lease、host から `ssh -p PORT root@127.0.0.1 uname -m` = `armv7`、`scp` 8 MiB の往復で `cksum` 一致 |
| p006 | xilinx-zynq-a9 | `make sd-image`（BOOT.BIN 無し）の image で p004・p005 と同じ（image の tool の回帰）。BOOT.BIN の鎖は QEMU では無し |

### 8.3 実機（T1、板の lock、U3 の serial）

| Phase | 道 | 流す物と合否 |
| --- | --- | --- |
| p003 | Pi 5（SSH で kernel を入れ替え、master.md の板。kernel は **p002 を含む main** から） | 起動、`sdhci: mmcblk0 ready` と root の mount（Pi 5 の SD の回帰、M5）、`gem: en0 ready`、DHCP、SSH の login、`ping` 100 回の loss 0（Pi 5 の Ethernet の回帰。**実機だけ**） |
| p004 | ZYBO（JTAG の kernel、`--append rootpart=/dev/mmcblk0p2`、`--initrd` 無し、card を差して） | `sdhci: … caps=69ec0080`（実機の値を記録）、base clock 50 MHz の行、`mmcblk0 ready`、root が SD、`/boot/boot0` が有る、`dd` の書き読み、`reboot` → JTAG で再起動して file が残る。もう 1 回 `--initrd root.img`（rootpart 無し）で root が ramdisk（H1） |
| p005 | ZYBO（JTAG の kernel、LAN を繋いで） | `gem: PHY 0 id 001cc915`、`link up 1000`（か 100）、DHCP の address（serial の `ifconfig en0`）、host から SSH と `scp` 8 MiB の往復、`ping -c 1000`（loss 0、RXUBR の行が出たら記録） |
| p006 | ZYBO（**JP5 = SD、ユーザー**、JTAG 無し） | 電源を入れて UART に `U-Boot SPL 2025.07`、kernel の `ZYBO PLATFORM board`、`login:`、SSH、`deploy.sh` で kernel を入れ替え `reboot` → 新しい kernel の banner（`uname -v` の build の時刻）で起動、`reboot` をもう 1 回。jumper を JTAG に戻して `jtag-hello.sh` PASS（JTAG の道が生きている） |

## 9. Phase と LW（8 LW ≈ 2〜3 時間）

| Phase | 目的と到達点 | LW | 依存 |
| --- | --- | --- | --- |
| p001 | この設計 | 10 | — |
| p002 | **汎用の SDHCI の core の分離**: `src/drivers/mmc/sdhci.c`・`include/drivers/mmc/sdhci.h`（rpi4-sdhci.c からの関数の移動、`kern_rtc_read_counter`、unit の配列、config）、`rpi4-sdhci.c` を glue に（公開の口は不変）、`tests/sdhci-host-test.c`（同じ検査）、`platform/arm64/vmunix.mk` の配線。到達点: rpi4・rpi5・amd64 の build warning 0、host 試験 PASS（同じ command の列）、`nm -u` の境界、**T1 の raspi4b の boot-test PASS**（QEMU の証拠）。Pi 4 の実機はユーザーの一覧 | 10 | — |
| p003 | **汎用の Cadence GEM の core の分離**: `src/drivers/ethernet/cadence-gem.c`・`.h`（rp1-ethernet.c からの移動、descriptor の幅 32/64、hook、RXUBR の flag と mask、PHY の走査）、`rp1-ethernet.c` を glue に（公開の口は不変、BCM54213PE の adapter、GPIO の reset の順は不変）、`tests/cadence-gem-host-test.c`（偽物を新しく書く）、`platform/arm64/vmunix.mk`（rpi5 に `mii-phy.c`）。到達点: rpi5 の build warning 0、host 試験 PASS（64 bit の register の列が今と同じ）、`nm -u` の境界、**実機の Pi 5 で SD の root・DHCP・SSH（T1、実機の証拠。板が空くまで「実装済み・T1 待ち」）** | 14（版 2、L15: 偽物の分 +4） | p002（Pi 5 の実機の kernel に含めるため。code は独立） |
| p004 | **ZYBO の SD と root**: `zynq-slcr.c`、`zynq-sdhci.c`、core の CSD、`zybo.c` の boot device（SD と ramdisk）、`rootfs.mk` の `root.ufs`、`make-zybo-sd-image.py`（BOOT.BIN 無しの image も作れる）と `disk-image.mk` の `sd-image`、`qemu-zynq.sh --sd`、`config-zybo.mk`。到達点: §8.2・§8.3 の p004、raspi4b の再確認 | 12 | p002、**WS286 p006**（task・割り込み・`zybo.c` の platform）、**WS286 p010**（root と login、`rootfs.mk`） |
| p005 | **ZYBO の LAN と SSH**: `zynq-ethernet.c`、`rtl8211e.c`、`mac0=`、`net.conf` の `en0`、`config-zybo.mk` の program（dhcpc・networkd・openssh ほか）、`qemu-zynq.sh --net`。到達点: §8.2・§8.3 の p005 | 14 | p003、p004（SD の root に sshd）、**WS286 p007**（userland）、**Q1: armv7 の package の build（zlib・openssl・openssh・ca-certificates）**、U4 |
| p006 | **SD からの起動**: `spl-build.sh` の fragment と `spl/boot.bin`、FIT の `vmunix.itb`・`system.dtb`（`zybo-sd.dts`）、`sd-image` の完成（BOOT.BIN 入り）、`deploy.sh`、README（jumper・`dd`・復旧）、`jtag-hello.sh` の再確認。到達点: §8.3 の p006（**ユーザーが JP5 を SD に**） | 12 | p004、p005、U1・U2 |
| p007 | 規約の全文の見直し（conformance。core 2 つ・glue 3 つ・PHY・tool） | 6 | p002〜p006 |

合計 **78 LW**（版 2。p001 の 10 を含む）。critical path: p002 → p004（WS286 p006・p010 の後）→ p005（WS286 p007 と package の後）→ p006 → p007。p002 と p003 は WS286 と独立で今すぐ始められる（arm64 の build だけ。別の担当でもよい。p003 の実機の Pi 5 の kernel に p002 を含める以外の依存は無い）。WS286 との順（ws.md の Q1 の案を直す）: WS286 p006 → p007 → **WS289 p002・p003（並行可）** → WS286 p010 → WS289 p004 → p005 → p006 → WS286 p008（PL。SSH で `scp design.bit` → `fpgaload` がそのまま使える）。

## 10. risk と敵対的な読み直し

| # | risk | 対処 |
| --- | --- | --- |
| R1 | core の分離で Pi 4 の SD の振る舞いが変わる（register の access の順・時計の分解能） | 関数の移動だけにし、host 試験の「同じ command の列」を p002 の受け入れに。`kern_rtc_read_counter` は arm64 で同じ generic timer。T1 の raspi4b。実機の Pi 4 はユーザーの一覧 |
| R2 | GEM の core で RP1 の 64 bit の道を壊す | p003 の host 試験の期待値は **今の code から採って固定**し、64 bit の register と descriptor の列が 1 word も変わらない事を確かめる。実機の Pi 5 で DHCP・SSH。Pi 5 が空かない間は p003 は T1 待ちで止め、p005 は進める（Zynq の 32 bit の道は QEMU と ZYBO で確かめる） |
| R3 | Zynq の GEM に無い register（`RXQBASEHI`/`TXQBASEHI` `0x4C8`/`0x4D4`、`DMACR` の DMA64）に書いて bus error・HRESP | `descriptor_width == 64` の時だけ書く。QEMU の model は受けるので **実機の p005 で最初に HRESP が出ない事**を見る |
| R4 | `drv_dma_map` が任意の buffer を map しないので SD の DMA が bounce になる（速さの期待外れ） | この WS は PIO（D3）。p004 で速さを測って U3 |
| R5（版 2 で格下げ） | SPL の `spl_fixup_fdt` が `memory@0` を壊す | defconfig 38 行 `# CONFIG_ARCH_FIXUP_FDT_MEMORY is not set` なので書き換わらない見込み（L8、`bootm-fdt.c` は未読）。kernel の `ARMV7 MEMORY total=`（511 MiB）を p006 の受け入れに残す。壊れていたら `cmain.c` の memory の読みに fallback（HAL の内部、hal.h は変えない） |
| R18（版 2） | FIT の staging `0x10000000`（`SPL_LOAD_FIT_ADDRESS`）と SPL の append する fdt（kernel の BSS）が何かと重なる | image の tool の検査（§5.3: kernel の `_end` + 64 KiB < `0x01F00000`、FIT < `0x1F000000`）。BSS は locore が消す。append が落ちる（SPL が `-ENOBUFS` などで止まる）時は FIT に `fdt` の node（`load = 0x01F00000`）を入れる逃げ道 |
| R19（版 2） | SD の image の p1 が FAT でない・壊れていると native の道で VFS の初期化ごと失敗（boot slot の mount） | host 試験で p1 を `mdir`、実機は UART の `vfs: boot0` の行。復旧は card の作り直し |
| R6 | SPL の binary が OCM の 192 KiB を超える（legacy の解析を足す） | defconfig の SPL は FAT・MMC・FIT を既に含む。p006 の最初に大きさを見る。超えたら `SPL_LOAD_FIT`・`SPL_FPGA`・`SPL_SPI_LOAD` を fragment で落とす |
| R7 | SPL の FAT が FAT32 を読めない、BootROM が FAT32 の partition を読めない | U-Boot の FAT は FAT12/16/32 を読む（`fs/fat`）。BootROM は未確認 → p006 の最初の実機の確認で駄目なら FAT16 32 MiB |
| R8 | boot mode が soft reset で JTAG に戻る・BootROM が `REBOOT_STATUS` を見て止まる | p006 で `reboot` を 2 回。戻らなければ `hal_reset` の `REBOOT_STATUS` の扱い（p005 の `BOARD_REBOOT_KEEP`）を Linux の `zynq_slcr_system_restart` と比べ直す |
| R9 | RTL8211E が address 0 で答えない（strap の違い）・PHY の ID の revision が違う | 走査（`phy_scan`）と log。ID は下位 4 bit を mask |
| R10 | RGMII の delay が合わず link は上がるが packet が通らない（1000 だけ、100 は通る） | `rgmii-id` の両方の delay を PHY に。駄目なら p005 で tx/rx の delay の 4 通りを boot parameter（`gem0.delay=`）で切り替えて試す |
| R11 | `USRIO` の bit 0 を Zynq で書く事の副作用 | Linux が zynq で書いている手順（§1.2）。QEMU の model にも register が有る |
| R12 | armv7 の package（openssl・openssh）の build が通らない・遅い | Q1 の依存として p005 の前に別の Queue で。通るまでは p005 の受け入れを「DHCP と `ping`」で区切り、SSH は後で（部分の clear は Queue で分ける） |
| R13 | `/boot/boot0` の FAT の `mv` が落ちて `vmunix.itb` が無くなる | `.new` → `sync` → rename、`.old` を残す。復旧は JTAG か card の作り直し |
| R14 | QEMU の SDHCI の model と Arasan の違い（例: `PRESENT_STATE` の CD、`sd-spec-version` 2 の capability の mask）で実機だけ落ちる | glue は CD を見るだけで初期化を試みる（§3.2）。実機の p004 で `caps=` の値を記録し、違いを design に書き戻す |
| R15 | SD の mode で `jtag-boot.sh` が `BOOT_MODE` の守りで何もしない（開発の流れが止まる） | 仕様どおり（守りは DDR を壊さないため）。更新は SSH、復旧は jumper を戻す（§5.5）。`jtag-reset.sh` は SD の mode でも効く |
| R16 | 2 つの SLCR の map（HAL と kernel）の unlock/lock が重なる | 1 CPU、HAL は boot と reset だけ。kernel の driver は spinlock の中で unlock → write → lock |
| R17 | `kern_rtc_read_counter` が platform の init の時点で動かない arch が将来 core を使う | core は false を `EIO` にして止まらない。armv7・arm64 は p005・rp1 で init の時点から動いている |

敵対的に読み直した点: (1) **「reset は gpio0 46」は誤り**で USB の PHY の物（§1.2）。GPIO の driver を作る Phase を立てずに済んだ。(2) **Falcon の既定の address は kernel と重なる**（§5.2）。fragment で移さないと最初の実機の起動で壊れる。(3) **`SPL_LEGACY_IMAGE_FORMAT` が defconfig で off**（`SPL_LOAD_FIT=y` のため）: 足さないと uImage が U-Boot として扱われ Linux の jump に乗らない。(4) **QEMU の PHY は address 7** で ZYBO は 0: 走査が無いと QEMU で LAN が動かない。(5) **`drv_dma_map` は coherent の割り当ての中だけ**: SDHCI の DMA を「bio の buffer を map する」と書くと実装で止まる。(6) **net.conf に無い interface は down**: `en0` を root に書かないと DHCP が始まらない。(7) **Pi 5 の回帰は実機だけ**（QEMU に RP1 が無い）: p003 の clear の条件に実機を明記した。(8)（版 2 で書き直し）legacy の uImage は header を load の 64 byte 下に置く（C1）: FIT にした。header の CRC の話は無くなった。(9)（版 2 で書き直し）legacy autoroot は zybo では `/boot/boot0` を mount しない（M1）: `rootpart=` は必須で、image の tool が必ず書き host 試験で見る。(10) SD と ramdisk の両方が有る時の `bios_id` は **initrd の有無**で決める（§3.6、H1）: card を差したままの JTAG の ramdisk の試験で root が SD に奪われない。(11)（版 2）RP1 の 64 bit の道を「1 bit も変えない」ために、core は `phy_scan = 0` の時 MDIO を読まず、GPIO の reset と PHY の初期化を `phy_setup` に入れて今の順を保つ（M3）。(12)（版 2）`descriptor[3] = 0` の 4 か所は 32 bit の stride では隣の descriptor を壊す（M3）。

## 11. ユーザー・Q1 の判断（U 項目）

| # | 問い | 勧め | 状態 |
| --- | --- | --- | --- |
| U1 | SD の boot mode への切り替え（JP5 の jumper）と card の差し込み・`dd` での書き込みはユーザーが行う。p006 の実機の確認の前に、Q1 がユーザーに頼む形でよいか | はい。手順は p006 の README（`lsblk` で device を確かめてから `dd`）。SwitchBot は繋がないまま（電源の入れ直しはユーザー） | 未決 |
| U2 | 復旧の道は JTAG だけ（U-Boot 本体を FAT に入れない）でよいか | はい（D5）。U-Boot 本体を足すと image と env の面倒が増える。JTAG は jumper を戻すだけ | 未決 |
| U3 | SD は PIO（DMA 無し）で始め、p004 の速さの実測（`dd`）が 2 MB/s を下回った時だけ SDMA（bounce）を Future Work から前倒しする | はい（D3） | 未決 |
| U4 | QEMU zynq の guest に host の `127.0.0.1` の転送 port から SSH で入って LAN を判定する（WS105・WS108・WS109 と同じ例外。serial の判定 U3 は WS286 で決定済み） | 認める（T1 が流す。AGENTS.md の検証の例外に 1 行） | 未決 |
| U5 | 固定の MAC `02:5a:59:42:4f:30`（locally administered）を既定にし、`mac0=` の boot parameter で変えられるようにする | はい（DHCP の lease と known_hosts が安定する。板が 1 つ） | 未決 |
| U6 | Pi 4 の SD（p002）と Pi 5 の Ethernet・SD（p003）の実機の確認: Pi 5 は T1 が実機で行い p003 の clear の条件にする（kernel は p002 を含む）。Pi 4 は QEMU の raspi4b を clear の条件にし、実機はユーザーの試験の一覧に入れる | はい（§8.3、R1・R2、M5） | 未決 |
| U7（版 2） | `src/drivers/platform/rpi4/rpi4-sdio-host.c`（Pi 4・Pi 5 の Wi-Fi の SDIO の host、1183 行。32 bit の書き込みと clock の手順が SDHCI と重なる）は「共通部分の分離」の範囲に**入れない**（Future Work に記録。SDIO の CMD52/53 と割り込みは SD の block の driver と形が違い、Wi-Fi の回帰（実機の Pi 4/5 の WPA2）が要る） | 入れない | 未決 |

**Q1 への依頼**: (1) armv7 の package の build（zlib・openssl・openssh・ca-certificates。`--machine armv7`、sysroot は WS286 p007 の後）を p005 の前の Queue に。(2) `src/kern/boot.c` の parameter の表に `mac0=` を足す事（p005 が行うが、boot.c は共有の file なので Q1 の承知）。vfs.c は変えない（H1 は platform の file で解く）。(3) ws.md の Phase の表を §9 に合わせる（この設計で直した）。(4) Master の Tools に `deploy.sh`・`qemu-zynq.sh` の `--sd`/`--net`・`make-zybo-sd-image.py` を p006 の後に。(5) ユーザーの試験の一覧に「Pi 4 の SD の起動（p002 の後、CSD の後にもう 1 回）」「JP5 を SD に（p006）」。(6) Future Work に `rpi4-sdio-host.c` の SDHCI の core への乗せ替え（U7）と SD の SDMA（§3.5）。（版 1 の (6)「WS286 design の訂正」は不要: WS286 §1.1 は正しい、L5）

## 12. ライセンスと転記

- zedBSD の新しい code（`sdhci.c`・`cadence-gem.c`・`rtl8211e.c`・`zynq-*.c`・tool・DTS）は Zlib。中身は tree の rpi4-sdhci.c・rp1-ethernet.c（Zlib）の移動と自分の code。
- 外の source からは **値と手順だけ**を block ごとに「Value(s) referenced:」「Procedure referenced:」で引く（Guardrail 2026-10-15: GPL の file の定数・表はデータとして出典を明示して使ってよい。code・comment・構造・symbol の名前は写さない）: Linux の `macb_main.c`（RXUBR の手順、tx clock の rate、MDC の表、USRIO）、`realtek.c`（RTL8211E の page と register）、`clkc.c`（SLCR の bit）、`sdhci-of-arasan.c`（quirk の意味）、U-Boot の `doc/usage/fit/`（FIT の node と property の名前。public な format、版 2 で legacy の header から替えた）、QEMU の `xilinx_zynq.c`（capareg、PHY の address）。rp1-ethernet.c が既に引いている OpenBSD `if_cad.c`（ISC）の Values referenced の注記は core に移す時にそのまま移す。
- U-Boot SPL（GPL-2.0+）: tree の外で build し（`spl-build.sh`）、`BOOT.BIN`（`spl/boot.bin`）は **tree に入れず配布しない**（p002 の方針のまま）。配布する時は zedBSD-firmware（`userland/firmware/zedbsd-firmware.mk` の形）に source の所在（tarball の URL と SHA-256、config の fragment）と `Licenses/gpl-2.0.txt` を添える（WS286 §4.4 の注記）— この WS の外。SPL の config の fragment（`spl-build.sh` の中の text）は設定で code でない。
- QEMU・Linux・U-Boot の file は scratchpad に置き、commit しない。

## 13. 決定の記録（2026-10-23）

| # | 原文 | 扱い |
| --- | --- | --- |
| D-a | ユーザー「メモリがあまり多くないのでSDドライバがあるとうれしいです。LANのドライバもついでに動かしてしまえませんか？単独ブートできるといいと思います。」（ws.md） | 到達目標（冒頭）。root を SD に（ramdisk の RAM を使わない）、LAN と SSH、SD からの起動 |
| D-b | ユーザー（WS286 の作成時）「まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。」 | SD の起動と SSH の更新は §5。SwitchBot は未接続のまま（WS286 の D2）→ `reboot` は SLCR の reset、電源の入れ直しはユーザー（D8） |
| D-c | ユーザー（Q1 経由、2026-10-23）「rpi4を参照するよりも、共通部分は分離しましょう。」＋ Q1 の指示（汎用の SDHCI の core と汎用の Cadence GEM の core、分離は ZYBO の glue より前の独立した Phase、Pi 4 の SD と Pi 5 の Ethernet の回帰の無い事の確認、platform 固有は薄い glue） | D1・D2、§3・§4、p002・p003（§9）、回帰の確認は §8（Pi 4 = QEMU raspi4b、Pi 5 = 実機）。WS286 design §5.3・§5.4・§11 の「写しで始める」「rp1-ethernet.c は変えない」は **この決定で置き換わる**（WS278 との衝突の懸念は、p003 が rp1-ethernet.c の公開の口を変えない事と、WS278 の p002 が main に入っている事で小さい。Q1 が WS278 の担当と重ならない順を決める） |

## 14. 選ばなかった案と理由

- **rpi4-sdhci.c を写して zynq-sdhci.c にする**（WS286 §5.3 の案）: ユーザーの決定で却下。同じ bug（BUG-288・295）の直しが 2 か所に増える。
- **Cadence GEM の core を OpenBSD/Linux から新しく書く**: rp1-ethernet.c が実機で動いている code。移動の方が risk が小さい。
- **SDHCI の core を dw-mmc.c の形（`move_data`・`transfer_run_tries`）に合わせて書き直す**: 振る舞いの変更になり Pi 4 の回帰の確認が重くなる。形（config の struct、`card_info`）だけ揃える。
- **SDMA を最初から**: D3。`drv_dma_map` の制約で bounce になり、速さの利点が小さい。
- **割り込みで SDHCI の完了を待つ**: PIO の 1 block ごとの buffer ready を割り込みにしても CPU は word の読みに使う。poll のまま。
- **U-Boot 本体（`u-boot.img`・`boot.scr`・`bootm`）**（WS286 §4.7 の版 1 の案）: D5。Falcon で 1 段少なく、kernel の更新は FAT の 2 つの file。U-Boot の shell が要る時（環境変数、network boot）は後で足せる（SPL は `uImage` が無ければ `u-boot.img` へ落ちる）。
- **legacy の uImage**（版 1 の案、版 2 で却下 C1）: SPL は header ごと `ih_load − 64` に読む（`spl_legacy.c` 47〜63 行）ので `0x00100000` の kernel では header が高位 OCM の alias に書かれる。FIT は data を `load` にそのまま置く。ITS は `dtc` で作れ `mkimage` は要らない。
- **FIT に DTB の node も入れる**（kernel ＋ DTB を 1 つに）: `spl_load_image_fat_os` は ARGS の file（`system.dtb`）を別に要求し `r2` はそれ（`spl.c` 763 行）なので DTB を 2 度持つ事になる。kernel だけの FIT ＋ `system.dtb` にし、append の失敗の逃げ道としてだけ残す（§5.1）。
- **legacy のまま trampoline を置く**（`0x02000040` の小さな code が kernel を `0x00100000` に写して飛ぶ）: 自前の loader が 1 つ増え、JTAG と違う入口になる。FIT で足りる。
- **FSBL（AMD、MIT）＋ `bootgen`**: OSS だけの決定（WS286 D2）。U-Boot SPL が JTAG と共通。
- **kernel の LMA を `0x02000000` に移して SPL の既定の footprint を避ける**: JTAG の道具・HAL の `ARMV7_KERNEL_PHYS`・DTB の場所が全部変わる。SPL の config を動かす方が zedBSD 側の変更が無い（D6）。
- **MAC を乱数に**: U5。
- **Zynq の GPIO の driver をこの WS で**: PHY に reset の GPIO が無い（§1.2）。USB（WS287）で要る時に。
- **root を legacy autoroot だけに頼る**: `rootpart=` を書く（D4）。両方が同じ結果になるので害は無い。
- **QEMU で BOOT.BIN の鎖を試す**（SPL を `-device loader` で `0x0` に）: QEMU zynq の OCM は `0xFFFC0000` だけ。SPL の text base `0x0` に RAM が無い（`-m` の DDR は `0x0` からだが BootROM の mode の設定・`BOOT_MODE` の register は SD にならない）。実機だけ。
- **SD の card detect で attach を止める**: 配線が未確認。log に出して初期化を試みる（§3.2）。

## 15. 版 1 → 版 2: design-reviewer の指摘と対応（2026-10-23、Opus の design-reviewer。Fable は使っていない）

| # | 重大度 | 指摘（要約） | 対応（節） |
| --- | --- | --- | --- |
| C1 | Critical | legacy の uImage は header を `ih_load − 64` = `0x000FFFC0`（高位 OCM の alias）に書く | FIT に替えた（D5、§5.1〜§5.3、§8.1 の検査、§14、R18）。`spl_load.h`・`spl_fit.c` を取って確かめた（§0 の表に追加: `include/spl_load.h` a69a4a47abbde3e6、`common/spl/spl_fit.c` cc069581648ab778） |
| H1 | High | SD が有ると常に 0x80 → card を差したままの JTAG の ramdisk の試験で root が SD に奪われる | initrd の有無で 0x80 を決める（§3.6、§7、§8.2・§8.3 に両方の起動の試験） |
| M1 | Medium | 「rootpart が無くても legacy autoroot で同じ結果」は誤り（legacy の FAT の mount は Pi だけ） | 記述を消し rootpart を必須に、失敗の道を §7、R19（§1.4、§3.6、§10 (9)） |
| M2 | Medium | RXUBR を RXDONE と一緒に mask していない | flag の時は同じ mask・unmask（§4.2、host 試験 (b)） |
| M3 | Medium | RP1 の MDIO が 2 回増える、GPIO の reset の順が変わる、pclk の fallback、`descriptor[3]` | `phy_scan = 0` なら core は MDIO を読まない、reset と bcm の初期化は `phy_setup` の中で今の順、pclk は glue、word 3 も条件付き（§4.2、§4.3、§4.8、§10 (11)(12)） |
| M4 | Medium | QEMU の「再読みで cksum」は buf cache から返る | QEMU を起動し直してから読む（§8.2） |
| M5 | Medium | Pi 5 の SD（200 MHz の glue）の確認が無い | p003 の Pi 5 の kernel に p002 を含め `mmcblk0 ready` を合否に（§3.7、§8.3、§9、U6） |
| M6 | Medium | CSD の復号の誤りで Pi の実機が止まり得る | CSD_STRUCTURE 0/1 だけ、他は `0xffffffff`、Pi の実機はユーザーの一覧（§3.8） |
| M7 | Medium | `rpi4-sdio-host.c` の扱いが無い | 範囲外を U7 に、Future Work（§11） |
| L1 | Low | v2 の divisor の符号化、意味の無い quirk の flag | flag を落とし、版で N の上限（§3.2、§3.4） |
| L2 | Low | card_present の扱いが 3 か所で違う | 「log だけ、初期化は試みる、落ちたら ENODEV」に統一（§3.2、§3.4、§7） |
| L3 | Low | net.conf の根拠が誤り（名の無い有線は既定で DHCP） | `en0` は必須でない（§1.5、§4.7） |
| L4 | Low | WP は MIO55 | 直した（§1.1） |
| L5 | Low | WS286 §1.1 は既に正しい、PS reset は推測 | 訂正の範囲を ws.md だけに、推測と明記、Q1 への依頼 (6) を差し替え（§1.2、§11） |
| L6 | Low | TX_DELAY = bit 12、RX_DELAY = bit 11 | 定義に従った（§4.5） |
| L7 | Low | PHY の address 7 の出典の行 | `xilinx_zynq.c` 125 行に（§1.2） |
| L8 | Low | R5 は不要の見込み、memreserve、header の CRC | R5 を格下げ、memreserve の漏れを記録、CRC の話は FIT で消えた（§5.1、§10） |
| L9 | Low | Zynq の ISR は read で消える | 記録（§1.2） |
| L10 | Low | rpi5 の build に `mii-phy.c` が無い | p003 で足す（§4.3、§9） |
| L11 | Low | host 試験の file の中は直す | 「検査の意味は変えない」に改めた（§3.7） |
| L12 | Low | SD の道に kernel の大きさの検査が無い | `--elf` の検査（§5.3、§8.1） |
| L13 | Low | 64 MiB の FAT32 の cluster の数 | FAT16（§5.3） |
| L14 | Low | `merge_config.sh` が option を黙って落とす | 全部の行を `grep -qx`（§5.2） |
| L15 | Low | p003 の 10 LW は少ない（偽物が要る） | 14 LW、合計 78（§4.8、§9） |

review が確かめた事（版 2 で変えていない所の裏付け）: SDHCI・GEM の core の API は関数の移動で乗る形、`kern_rtc_read_counter` は arm64 でも同じ counter で boot_worker の中なら動く、SPL の fragment の address の整合と Kconfig の依存、jump の register、`spl_start_uboot() = 0`、`rootpart=` の native の道で `/boot/boot0` が mount される事、非 coherent の DMA と `hal_pmem_map_uncached` の窓（256 MiB、使うのは約 1 MiB）、QEMU の capareg・`-drive if=sd`・`-nic` の順・SLCR の lock、hal.h の変更は不要（HAL の内部の変更だけ）。review が確かめなかった事（推測のまま）: U-Boot の `arch/arm/lib/bootm-fdt.c`（R5）、partition.c の範囲の検査（M6）、Zynq の UG585 の GEM の `0x4C8`/`0x4D4` と ISR の型、ZYBO の回路図（PHY の reset、SD の電源）。
