<!-- awesome-plan project=zedbsd record=ws313-design -->

# WS313 設計: ODROID-C5（Amlogic S905X5M「S7D」）

p001 の調べと設計（2026-10-29、P7）。ユーザーの指示（2026-10-29「実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」）で、調べは短くし、実装を先に揃える。
ここの事実は下の出典から取った。**実機では何も確かめていない**（「確認済み」は資料の上の事、「推定」は資料からの推定）。

## 出典

| 記号 | 物 | 版 |
| --- | --- | --- |
| HK | github.com/hardkernel/kernel_common_drivers（Hardkernel の Linux の SoC の driver と DTS、多くは (GPL-2.0+ OR MIT)） | branch odroids7d-6.12.y @ 34daba993d84 |
| HKL | github.com/hardkernel/linux（上の submodule を持つ本体、Linux 6.12.38） | odroids7d-6.12.y @ 9ea2aa831a58 |
| UB | git.odroid.com/yocto/uboot（Hardkernel の U-Boot v2023.01、BL33） | odroidc5-v2023.01 @ 3c17bc8024e5 |
| UP | Linux（torvalds） | c339f4010dea（v7.3+、2026-10-28） |
| WEB | https://www.hardkernel.com/shop/odroid-c5/ ・ cnx-software の記事 | 2026-10-29 に読んだ |

wiki.odroid.com は HTTP 403 で読めず（その site は使うのを止めた）。UART の connector の pin 配置と boot の媒体の順は未確認。clone は `build/ref/`（git に入れない）。

## 1. board と SoC（確認済み）

- S905X5M（codename S7D）、Cortex-A55 ×4（MPIDR 0x000/0x100/0x200/0x300、Aff1 で番号）。RAM 4 GB DDR4（DRAM は 0 から）。[WEB][HK]
- 外への口: GbE RJ45、USB 2.0 host ×4（SoC の host 1 本の後ろの hub）、micro-USB OTG、HDMI 2.0、eMMC の socket、microSD、line-out、IR、LED。USB 3・PCIe は無い（C5 の DTS は pcie を消す）。[WEB][HK]
- debug UART は 921600 baud。[WEB]。電源は 7.5〜15.5 V（12 V/2 A 推し）。SPI flash は無い（U-Boot の env は MMC の 0x3f0000）。[WEB][UB]
- **GPU は Mali-G310 V2（Valhall）**。ユーザーの情報「Mali G31 (Valhalla)」は誤り（G31 は前の S905X4/ODROID-C4 系の Bifrost）。[WEB] G310 は v10 の CSF の世代の推定で、それなら firmware（mali_csffw.bin）が要る（GPU の担当が `plan/ws313/gpu.md` で確かめる）。実機で GPU_ID の register を読んで確かめる。
- **Ethernet の MAC は DesignWare EQoS 4.x**（`snps,dwmac-4.00`、0xfe368000、SPI 74）。**PHY は外付けの RGMII の 1G**（C5 の DTS は内蔵の 10/100 PHY の int_mdio を消し、ext_mdio の address 0、reset GPIOH_9）。ユーザーの情報「内蔵 MII PHY (rmii?)」は、Hardkernel の U-Boot の DTS（内蔵 PHY の設定のまま）から来たと見られ、Linux の DTS とは違う。PHY の chip 名は資料に無い（RTL8211F の推定、実機で MII の ID を読む）。[HK][UB]

## 2. 起動の道

**確認済み**: BL1（ROM）→ BL2 → BL30（AO の RISC-V）・BL31（TF-A 2.7）・BL32（OP-TEE）→ BL33（Hardkernel の U-Boot v2023.01）。BL2〜BL32 は Amlogic の署名つきの binary（UB の submodule）。U-Boot は `boot_default` で mmc_list（起動した媒体が先）の part 1〜3 の `boot.scr` を 0x1080000 に読み `source` する。`booti` あり、`bootefi` は既定で入る見込み（未確かめ）。HDMI は U-Boot が `init_display` で出す。U-Boot は simple-framebuffer の node を足さない。[UB]

**決め（推し、ユーザーの判断の対象は §8）**:
- U-Boot は Hardkernel の物をそのまま使い（作り直さない）、zedBSD の既存の arm64 の vmunix（Linux の arm64 Image の header つき、text_offset 0x80000）を `booti` で載せる。DTB は x0。kernel は 0x80000 に置かれる（DRAM の先頭 0〜0x80000 は dmc_monitor の予約、kernel はその直後）。
- DTB は Hardkernel の DTS（HK の s7d_s905x5m_odroidc5.dts）を build の時に cpp と dtc で作る（host で作れることを確かめた: `build/tmp/c5dtb/c5.dtb`、77,244 byte）。zedBSD の追加（/chosen の simple-framebuffer）を足した物を FAT に置く。
- `boot.scr`（zedBSD が作る）: U-Boot の OSD を 32 bit で 0x7f800000（DTS の logo_reserved、8 MiB）に向け直し、kernel・DTB を読み、`booti`。fdt の命令が使えれば framebuffer の node をその場で足す。HAL は simple-framebuffer があればそれを console にし、無ければ UART だけで進む。
- 媒体: microSD に zedBSD の image（MBR、part 1 FAT32 の boot、part 2 UFS の root）。bootloader の区画（sector 1〜、env 0x3f0000）を避けて part 1 は 8 MiB から。U-Boot の binary（`u-boot.bin.sd.bin`、Amlogic の blob を含む）は zedBSD の image に入れず、利用者が Hardkernel の物を書く（再配布の license は未確認、§8）。

## 3. 各 block（register の値は出典の file から、code は写さない）

| block | 場所・番号 | 使う物 | 出典 |
| --- | --- | --- | --- |
| GIC | GIC-400（v2）、GICD 0xfff01000、GICC 0xfff02000 | 既存の `bsp-rpi-common/gic.c` | HK meson-s7d.dtsi、UP amlogic-s7d.dtsi |
| timer | arm,armv8-timer、PPI 13/14/11/10（非 secure の物理 = INTID 30） | 既存の `clock.c`（CNTFRQ は firmware が設定） | 同上 |
| PSCI | 0.2（UP は 1.0）、smc。SYSTEM_RESET 0x84000009・SYSTEM_OFF 0x84000008 | Pi 5 と同じ PSCI の呼び方（`bsp-s905x5m/board.c`） | 同上、HK aml_reboot |
| UART | uart_B 0xfe07a000（apb4 0xfe000000 の中）、SPI 169 edge、24 MHz xtal。WFIFO 0x0・RFIFO 0x4・CONTROL 0x8・STATUS 0xc（RX empty bit 20、TX full bit 21）・MISC 0x10・REG5 0x14 | `bsp-s905x5m/uart.c`（U-Boot の速さを保つ）、kernel の入力は `rpi4-console.c` の C5 の枝 | HK、UP meson_uart.c |
| 予約の memory | secmon 0x05000000〜0x073fffff（no-map）、ramoops 0x07400000、log 0x04e00000、iotrace 0x04f00000、logo 0x7f800000（8 MiB）、dmc 0〜0x7ffff | HAL の `fdt.c` が /reserved-memory（status を見る）と /memreserve/ を除く | HK s7d_s905x5m_odroidc5.dts |
| RAM | memory@0 に `linux,usable-memory` 0〜2 GB（U-Boot が reg を足すか未確かめ） | Linux と同じく usable-memory を優先 | HK、UB |
| SD | sd_emmc_b 0xfe08a000 SPI 177（"amlogic,meson-v8-mmc"）、pin GPIOC_0..5、CD GPIOC_6、電源 GPIOH_6、電圧 GPIODV_4 | 新しい meson の MMC の driver | HK、UP meson-gx-mmc.c |
| eMMC | sd_emmc_c 0xfe08c000 SPI 178 | 後（範囲は SD を先） | HK |
| USB | CRG の xHCI 0xfe340000 SPI 130（32 bit DMA）、USB2 PHY1 0xfe348000/0xfe34c000、reset 0xfe002000 の bit 4・9 | 既存の pci-xhci.c に MMIO で渡す glue、PHY の起こし | HK crg.c・phy-meson-*.c |
| Ethernet | EQoS 0xfe368000 SPI 74、glue PRG_ETH0/1 0xfe364000、MDIO mux 0xfe360000、RGMII GPIOZ_0..12 func 6、MDC GPIOH_11、PHY reset GPIOH_9 | 既存の dw-eqos.c と glue | HK、UP dwmac-meson8b.c・mdio-mux-meson-g12a.c |
| clock・reset・pin | clkc 0xfe000000（gate 0xfe000044/48）、reset 0xfe002000（pulse 0x00、level 0x40）、pinctrl 0xfe004000（GPIO 0xfe0040c0）、多くの clock は SCMI（smc） | SoC の小さい helper（U-Boot が既に起こした物を前提に最小限） | HK s7d.c・pinctrl-meson-s7d.c |
| display | VPU 0xff900000（OSD_V4）、HDMI TX 0xff000000（amhdmitx-s7d）、VIU vsync SPI 197 | まず U-Boot の HDMI の mode を使い OSD1 の address だけを替える（scanout の規則に合う） | HK mesons7d_drm.dtsi |
| GPU | Mali-G310 V2 0xfe400000（meson-valhall.dtsi） | 新しい driver `src/drivers/gpu/mali/` | HK、WEB |

HAL の API（`include/hal/hal.h`）の変更は要らない: GIC v2・generic timer・PSCI・FDT・名前つきの handoff は既存の口で足りる。HAL の実装の変更は `bsp-rpi-common/framebuffer.c` から mailbox の部分を `framebuffer-mailbox.c` に分けた事と、`fdt.h` の記録に bootloader の framebuffer の欄を足した事（Pi の parser は 0 のまま）。

## 4. 置き場所

- HAL: `src/hal/arm64/bsp-s905x5m/`（board.h・locore-board.h・uart.c・fdt.c・board.c・activity-led.c・framebuffer-boot.c）。共通の物は `bsp-rpi-common/`（名前は rpi_ のままの既存の口。改名はこの WS の範囲の外）。
- build: `ZEDBSD_PLATFORM := odroid-c5`、`ZEDBSD_BOARD := odroid-c5`、`BUILD := build/odroid-c5`、`-DHAL_BOARD_ODROID_C5`。`platform/arm64/vmunix.mk` の board の表、`platform/arm64/s905x5m.mk`（SoC の driver の一覧）、`platform/arm64/mali.mk`（GPU）。config は `config/ci/config-odroid-c5.mk`（text の console）と `config-odroid-c5-keiland.mk`（desktop、CONFIG_DRIVER_MALI_GPU=y）。
- kernel: `src/kern/platform/odroid-c5.c`、SoC の driver は `src/drivers/platform/s905x5m/`、GPU は `src/drivers/gpu/mali/`。
- 媒体: `platform/arm64/odroid-c5.mk`（DTB の取得と build、boot.scr、image）と `platform/arm64/tools/make-odroid-c5-hdd-image.py`。

## 5. 実装の Phase（実機なしで揃え、各 Phase は「実装済み・実機未試験」で記録）

見積りは LW（8 LW ≈ 2〜3 時間）。

| Phase | 内容 | 見積り | 担当 |
| --- | --- | --- | --- |
| p002 | HAL の BSP（起動・UART・FDT・GIC・timer・PSCI の SMP と reset）と build の配線、config | 6 | P7（済: build 通る） |
| p003 | 媒体: DTB の build、boot.scr、SD の image、夜の試験の手順と script | 6 | P7 |
| p004 | SD（meson の MMC）と root の mount | 10 | 配下 A |
| p005 | USB（PHY・CRG の xHCI）と keyboard・mouse | 10 | 配下 A |
| p006 | Ethernet（EQoS・glue・RGMII の PHY） | 8 | 配下 A |
| p007 | display（U-Boot の mode の OSD1 の flip） | 8 | 配下 B |
| p008 | GPU（Mali-G310 の CSF、firmware、MMU、job）と Vulkan の device | 40 以上（一日では骨組みまで） | 配下 B |
| p009 | desktop（Keiland の config、graphical login） | 6 | P7 |
| p010 | Audio（line-out） | Future Work の案 | — |
| 最後 | 規約の全文の見直し | 6 | — |

## 6. 夜の実機の試験（手順の本体は `plan/ws313/tests/README.md` と script）

順: (1) U-Boot の prompt と boot.scr の実行 → (2) HAL の `ODROID-C5 ENTRY`・`FDT PASS`・SMP 4 core → (3) framebuffer の試しの模様（三色）が HDMI に出るか → (4) SD の root の mount と login prompt（UART）→ (5) USB の keyboard → (6) Ethernet の link と DHCP → (7) GPU_ID → (8) desktop。
判定は UART の対話（AGENTS.md §9 の例外は ZYBO・RG350 にしか無いので、C5 の UART の対話を判定に使ってよいかは Q1 がユーザーに確かめる）と HDMI の画面の写真（ユーザー）。

## 7. ユーザーに準備を頼む物

- USB-UART の adapter（3.3 V、921600 baud を出せる物。CP2102・FT232 など）と配線（C5 の UART の connector の pin 配置は wiki が読めず未確認、Hardkernel の USB-UART kit が楽）。centris の USB につなぐ。
- 12 V/2 A の電源（5.5/2.1 mm）、入れ直しの SwitchBot の plug（どの plug か）。
- microSD（8 GB 以上）と、Hardkernel の bootloader（`u-boot.bin.sd.bin`、Hardkernel の Ubuntu の image から取るか、配布の物）。媒体を書く手段（centris の SD の reader か、ユーザーの手）。
- HDMI の display、USB の keyboard・mouse、LAN の cable（DHCP のある網）。

## 8. 人の判断が要る点（推しつき）

1. **起動の道**: Hardkernel の U-Boot + boot.scr + booti（推し。今の vmunix がそのまま載る）／ U-Boot の bootefi で zedBSD の UEFI の loader（amd64 の物を arm64 に移す作業が要る）／ U-Boot を作り直す（BL2 の署名で困難）。
2. **bootloader の binary の扱い**: zedBSD の image に入れず、利用者が Hardkernel の物を書く（推し、license の確認まで）／ 取得・検証して image に入れる（Amlogic の blob の再配布の条件を確かめてから）。
3. **GPU の firmware**: G310 が CSF なら mali_csffw.bin（Arm の license の binary、linux-firmware で配布）が要る。他の firmware と同じく取得・検証・同意の形にする（推し）。
4. **DTB の出所**: Hardkernel の DTS を build で取得して dtc で作る（推し）／ Hardkernel の Ubuntu の image の DTB を写す。
5. **UART の対話を判定に使う**（§9 の例外を C5 に広げるか）。
6. **範囲**: eMMC・OTG・Audio・HDMI の mode の変更（U-Boot の mode を使うだけでよいか）は後回し（推し）。
