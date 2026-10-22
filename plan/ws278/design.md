# WS278 Raspberry Pi 5 対応の設計

- 版 1（2026-10-21 P3、ws278-p001、q1542）。Fable の design-reviewer は使わない（2026-10-21 ユーザー「Pi5の対応は、Fableによるレビューは不要です。」）。
- 資料: Raspberry Pi の Linux（`raspberrypi/linux` rpi-6.12.y の `arch/arm64/boot/dts/broadcom/bcm2712.dtsi`・`bcm2712-ds.dtsi`・`bcm2712-rpi.dtsi`・`bcm2712-rpi-5-b.dts`・`rp1.dtsi`）、
  `raspberrypi/firmware` commit 3d301dd924bcd758a4c8cb19fe8531031f033f43 の `boot/bcm2712-rpi-5-b.dtb`・`boot/bcm2712d0-rpi-5-b.dtb`（`dtc` で読んだ）、
  `raspberrypi/documentation` master の `config_txt/boot.adoc`・`config_txt/conditional.adoc`・`config_txt/video.adoc`・`raspberry-pi/eeprom-bootloader.adoc`・`processors/bcm2712.adoc`。
- license: 値（address・割り込みの番号・clock・bit）は hardware の事実として使い、出典は block ごとの「Value(s) referenced:」で書く（Guardrail の MIT・GPL の出典の節）。Linux の code・構造・名前は写さない。

## 1. BCM2711（Pi 4）と BCM2712（Pi 5）の差

| 項目 | Pi 4（今の zedBSD） | Pi 5 | zedBSD への影響 |
| --- | --- | --- | --- |
| boot の firmware | SD の FAT の `start4.elf`・`fixup4.dat`（VideoCore が読む） | 基板の SPI EEPROM の bootloader が FAT の `config.txt`・kernel・dtb を直に読む。`start*.elf` は不要 | FAT に置くのは `config.txt`・`cmdline.txt`・`vmunix`・dtb（C1 と D0 の 2 つ）・license。firmware の cache は dtb だけ |
| 既定の kernel 名 | `kernel8.img` | `kernel_2712.img`、無ければ `kernel8.img`。`kernel=` で上書き | `kernel=vmunix` を `[pi5]` に書く |
| 読み込みの番地 | `kernel_address=0x80000`（vmunix は 0x80000 に link） | `kernel_address` は Pi 5 の bootloader も読む（未確認、実機で確かめる）。0〜0x80000 は TF-A（BL31、`/reserved-memory/atf@0`、no-map） | vmunix は Pi 4 と同じ 0x80000。HAL は 0〜1 MiB を予約済み（page.c）。番地が違えば locore が別の番地で止まるので、実機の最初の試験で確かめる（§6 の R1） |
| dtb の選び方 | `device_tree=bcm2711-rpi-4-b.dtb` | 基板の stepping で firmware が `bcm2712-rpi-5-b.dtb`（C1）か `bcm2712d0-rpi-5-b.dtb`（D0、2 GB 版と新しい版）を選ぶ。`os_check=1` は合う dtb が無いと止める | 2 つとも置き、`device_tree=` は書かない。D0 は uart10 の割り込みが SPI 120（C1 は 121）、pinctrl の compatible が違う → 番地・割り込みは dtb から読む。2026-10-22 の実機（D0）は C1 の dtb を受け取った → pin controller の stepping は chip の register から決める（p006 の 3 回目） |
| EL と CPU の起こし方 | EL2 で入る。他の CPU は armstub の spin-table | EL2 で入る（EL3 に TF-A）。他の CPU は PSCI（`enable-method = "psci"`、`method = "smc"`） | p002 は 1 CPU（smp.c は spin-table の CPU だけ数えるので PSCI の CPU は数えない）。SMP は PSCI の CPU_ON（p003） |
| CPU | Cortex-A72、MPIDR の Aff0 が core 番号 | Cortex-A76、MT=1、core 番号は Aff1（reg 0x000/0x100/0x200/0x300） | locore・smp.c の Aff0 の索引を Aff1 に（p003）。p002 の 1 CPU では影響なし（boot CPU は Aff0=0） |
| page・cache | 4 KiB | A76 は 4K/16K/64K。Raspberry Pi OS は 16K の kernel を使う | zedBSD は 4 KiB のまま。L1 64 KiB・L2 512 KiB/core・L3 2 MiB、line 64 B。cache 保守は CTR_EL0 から（変更なし） |
| 周辺の番地 | 0xfc00_0000〜0xff80_0000（4 GiB の下） | `soc` は 0x10_0000_0000〜（uart10 0x10_7d00_1000、mailbox 0x10_7c01_3880、GIC 0x10_7fff_9000/0x10_7fff_a000、SD の sdio1 0x10_00ff_f000、sdio2 0x10_0110_0000、V3D 0x10_0200_0000、PCIe 0x10_0010_0000〜） | locore の起動時の表に L1[65]（0x10_4000_0000〜0x10_7fff_ffff）の device の block を足す。space.c は HAL の device を `hal_space_map_device` で地図に置く（今の `map_device_block` は 3〜4 GiB だけ） |
| 割り込み | GIC-400 | GIC-400（同じ） | gic.c はそのまま。SPI は dtb から |
| timer | generic timer 54 MHz、PPI 30 | 同じ（54 MHz、PPI 14/11/10 = INTID 30/27/26） | clock.c はそのまま |
| debug UART | PL011（GPIO 14/15、BT と共用） | 基板の 3 pin の「UART」connector が BCM2712 の PL011（uart10、clock 9.216 MHz）。40 pin の GPIO 14/15 は RP1 の uart0 | 早い console は 0x10_7d00_1000 を固定で使う（dtb を読む前）。firmware が有効にしていれば baud は触らない。serial の console は Pi 4 と同じく既定で無効（2026-10-12 ユーザー「RPIのシリアルポートは使っていません。コンフィグで無効化してください。」） |
| firmware の mailbox | bus address は `phys \| 0xc000_0000` | `firmware` の node は `dma-ranges;`（等しい）、`soc` に dma-ranges 無し → 物理の address をそのまま | mailbox.c は Pi 5 で alias を付けない。buffer は 4 GiB の下 |
| framebuffer | mailbox で確保（`brcm,bcm2708-fb`） | dtb に `brcm,bcm2708-fb` の node がある → mailbox の framebuffer は Pi 5 でも動く | framebuffer.c はそのまま（返る address の上位 2 bit を落とすのは Pi 5 でも 1 GiB の下なら無害）。`hdmi_cvt` 等の旧い設定は Pi 5 で効かない事があるので `framebuffer_width/height` だけ |
| SD card | eMMC2（`brcm,bcm2711-emmc2`、SDHCI、32 bit access のみ） | sdio1（`brcm,bcm2712-sdhci`・`brcm,sdhci-brcmstb`、SDHCI、clock 200 MHz、card 検出は aon_gpio5、電源は aon_gpio4、IO 電圧は aon_gpio3） | `rpi4-sdhci.c`（PIO の汎用の SDHCI）を使う。基準の clock は Pi 5 で 200 MHz を渡す（caps に頼らない）。firmware が読み終えた card の状態を CMD0 から初期化し直す |
| reset・電源断 | PM の watchdog（0xfe10_0000） | PSCI の SYSTEM_RESET（0x8400_0009）・SYSTEM_OFF（0x8400_0008）、smc | asm.c の `hal_reset`・`hal_poweroff` に Pi 5 の分岐 |
| 停止の監視の表示 | HVS（0xfe40_0000）の display list に書く | HVS は 0x10_7c58_0000 で形も違う（HVS6）。0xfe40_0000 は 4 GB 以上の Pi 5 では RAM | watch.c の HVS の対応付けを Pi 5 で行わない（framebuffer の文字だけ） |
| PCIe と RP1 | VL805 の xHCI（PCIe 1 本） | RP1（PCIe x4 の pcie2 の先）に USB（DWC3 の xHCI ×2）・Ethernet（Cadence GEM）・GPIO・UART・I2C・SPI。`pciex4_reset=0` で firmware の PCIe の設定（窓・BAR）を引き継げる。RP1 の周辺は CPU から 0x1f_00xx_xxxx、RP1 の DMA は物理の address。割り込みは RP1 の MSI-X → MIP0 → GIC SPI 128〜 | p004 以降。最初は `pciex4_reset=0` で引き継ぎ、xHCI は poll で始める |
| Ethernet | GENET（SoC 内） | RP1 の Cadence GEM（`cdns,macb`）、PHY は BCM54213PE（RP1 GPIO 32 で reset） | 新しい driver（p005）。PHY の driver は流用の見込み |
| WiFi・BT | CYW43455、SDIO は旧 EMMC（Arasan）、BT は PL011 | 同じ CYW43455。SDIO は sdio2（SDHCI 3.0、`brcm,bcm2712-sdhci`）、WL_ON は BCM2712 の GPIO 28（`gio` 0x10_7d50_8500）。BT は `uarta`（0x10_7d50_c000、`brcm,bcm7271-uart`＝16550 系、reg-shift 2）、BT_ON は GPIO 29 | bcm43455 の driver（`src/drivers/wifi/bcm43455/`）を流用し、SDIO の host を sdio2 に作る（p006）。NVRAM・CLM は Pi 5 の物。BT は p007 |
| GPU | V3D 4.2（`src/drivers/gpu/bcm2711/`）、HVS5 | V3D 7.1（`brcm,2712-v3d`、hub/core0/sms、SPI 250/249）、HVS6（`brcm,bcm2712-hvs`、IOMMU4 の後ろ）、pixelvalve・HDMI も別 | 既存の driver は V3D 4.2 の register・QPU の ISA・control list に固い。V3D 7.1 は QPU の命令・TLB・packet が変わる（Mesa は v42 と v71 で分ける）。別の WS に分けるのを提案（§4 D2）。当面は firmware の framebuffer の console |

## 2. 方式

- **board を compile 時に選ぶ**: `ZEDBSD_PLATFORM := rpi5`、`ZEDBSD_ARCHITECTURE := arm64`、`ZEDBSD_BOARD := rpi5`。platform の directory は Pi 4 と同じ `platform/arm64`、BUILD の既定は `build/rpi5`（riscv64 の 2 つの board と同じ形）。
  vmunix.mk は `-DHAL_BOARD_$(BOARD)` と board ごとの source の一覧（`ARM64_BOARD_SOURCES_rpi4`・`_rpi5`）を持つ。
- **rpi4 の HAL・driver に Pi 5 の分岐を足す**（ws.md の「既存の rpi4 を土台に、差を足す」）。分岐は `#if defined(HAL_BOARD_RPI5)` で、rpi4 の object は前処理の後に変わらない（rpi4 の kernel は同じ）。
  Pi 5 だけの file は `src/hal/arm64/bsp-rpi5/`（早い UART、PSCI）と `src/kern/platform/rpi5.c`（kernel の platform の結び）。
- **HAL から kernel へ**は今の規則のまま: 汎用の handoff の構造体と、名前で引く値。Pi 5 は `rpi5.fdt`・`rpi5.sdhci`・`rpi5.framebuffer.*`・`boot.command-line`。hal.h は変えない。
- **image**: 今の `make-rpi4-hdd-image.py`（MBR、FAT32 の boot、UFS の root）に board の引数を足し、Pi 5 は firmware の file の一覧だけを変える。FAT の配置・UFS の root の位置は Pi 4 と同じ。
- **firmware の cache**: `platform/arm64/rpi5-boot-firmware.mk`。Pi 4 と同じ commit の `boot/bcm2712-rpi-5-b.dtb`・`boot/bcm2712d0-rpi-5-b.dtb`・`boot/COPYING.linux`（dtb の GPL-2.0）を大きさと SHA-256 で確かめて `build/sources/firmware/rpi5-boot/<commit>/` に置く。license の確認は firmware の共通の手順（`userland/firmware/zedbsd-firmware.mk`）。

### config.txt（Pi 5）

```
[pi5]
kernel=vmunix
kernel_address=0x80000
pciex4_reset=0        # p004 から（RP1 の PCIe の設定を引き継ぐ）。p002 では書かない
framebuffer_depth=32
framebuffer_ignore_alpha=1
framebuffer_width=1280
framebuffer_height=720
disable_overscan=1
```

`arm_64bit` は Pi 5 で無視される（64 bit だけ）。`enable_gic` は Pi 5 で意味が無い。`uart_2ndstage=1` は serial の build の時だけ（bootloader の log を debug UART に出す）。

## 3. Phase の分け方

| Phase | 内容 | 依存 | 確認 |
| --- | --- | --- | --- |
| p002 | `config/ci/config-rpi5.mk`、Makefile・vmunix.mk の board、HAL の Pi 5 の分岐（locore の表、早い UART、FDT、mailbox、space の device の対応付け、PSCI の reset・電源断、監視の HVS を外す）、`src/kern/platform/rpi5.c`（console・SD の root）、firmware の cache、image。1 CPU、console は framebuffer（serial は option） | p001 | build（warning 0）、host の FDT の試験（Pi 5 の C1・D0 の dtb を HAL の parser で読む）、rpi4 の kernel が同じ事（前後の kernel.elf を比べる）、image の検査。実機: HDMI に kernel の log と login の prompt |
| p003 | SMP: PSCI の CPU_ON、A76 の Aff1 の索引（locore・smp.c）、GIC の CPU interface の番号 | p002 | 実機で `cpus=4` の起動、`top` の 4 CPU |
| p004 | RP1: `pciex4_reset=0` で引き継ぐ。RP1 の xHCI（DWC3、2 つ）を既存の xHCI の driver（`pci-xhci.c`、MMIO の base を渡す形）で、最初は poll。USB の keyboard で login | p002 | 実機で USB keyboard の login、USB storage |
| p004b | RP1 の割り込み（RP1 の MSI-X → MIP0 → GIC）。poll を割り込みに | p004 | 実機 |
| p005 | RP1 の Ethernet（Cadence GEM の新しい driver、PHY の reset は RP1 GPIO 32） | p004 | 実機で DHCP・ping |
| p006 | WiFi: sdio2 の SDIO host（SDHCI 3.0 の CMD52/53）、WL_ON（GPIO 28）、bcm43455 の driver を流用、Pi 5 の NVRAM・CLM | p002 | 実機で scan・接続 |
| p007 | BT: uarta（16550 系）、BT_ON（GPIO 29）、既存の HCI の class | p006 | 実機 |
| p008 | 規約の全文の見直し（WS が変えた C の全部） | p002〜 | build・host 試験 |
| 別 WS（提案） | GPU: V3D 7.1 の Vulkan と HVS6 の display | p002 | — |

## 4. ユーザーの判断が要る点

- **D1**（serial の console）: Pi 5 の debug UART は BT と共用でない専用の connector。2026-10-12 の決定（RPi の serial は使わない、既定で無効）を Pi 5 にも当て、既定 `CONFIG_RPI5_SERIAL_CONSOLE := n`、option で y とした。Pi 5 で既定を y にするかはユーザーに聞く（推し: n のまま。実機の解析で要る時だけ y の build）。
- **D2**（GPU）: V3D 7.1 の Vulkan は今の bcm2711 の driver（V3D 4.2）の枝ではなく大きな仕事になる。WS278 の中で始めるか、別の WS にするか（推し: 別の WS。WS278 は firmware の framebuffer の console と、Keiland 無しの console の login まで）。
- **D3**（試験の機材）: Pi 5 の実機と、HDMI の monitor（micro HDMI の HDMI0）、USB keyboard（p004 から）、あれば Raspberry Pi Debug Probe（3 pin の UART、serial の build の時）。ユーザーの手元の Pi 5 の型（RAM、C1 か D0）を知りたい。

## 5. 試験の方法

QEMU に raspi5 の machine は無い。確認は host の試験と実機だけ。

- host（担当が流す）: build（warning 0）、Pi 5 の 2 つの dtb を HAL の FDT の parser で読む試験（uart・mailbox・GIC・SD・memory・CPU の数と PSCI）、image の検査（MBR・FAT32・file の一致・UFS の root）、rpi4 の kernel.elf が変わらない事。
- 実機（Q1 経由でユーザーに頼む。手順は `plan/ws278/tests/rpi5-hw-test.md`）:
  1. `build/rpi5/hdd-image.img` を SD card に書く（`dd if=… of=/dev/sdX bs=4M conv=fsync`）。
  2. Pi 5 の HDMI0 に monitor をつなぎ、SD を入れて電源を入れる。
  3. 期待: bootloader の画面の後、framebuffer に `RPI5 ENTRY`… の HAL の log、kernel の log、`login:` の prompt。写真を撮る。
  4. 止まった時: 画面の最後の行の写真。serial の build（`CONFIG_RPI5_SERIAL_CONSOLE=y`）と Debug Probe があれば 115200 の log。
  5. p004 以降: USB keyboard で login、`uname -a`・`dmesg | head`・`df`。

## 6. 危険と確かめる点

- R1: Pi 5 の bootloader が `kernel_address=0x80000` を守るか。守らなければ 0x200000 等に置かれ、locore の絶対の飛び先がずれる。対策（p002 で実装済み）: Pi 5 の locore は読み込まれた番地が 0x80000 より上なら、header の image size の分を 0x80000 へ写してから続ける（先に stub だけを写してそこから全体を写す）。残る危険: 0x80000 へ写した後の bss が、firmware が kernel の直後に置いた dtb に重なる場合（firmware は今まで dtb を高い所に置く）。
- R2: dtb の置き場所が 1 GiB の上なら起動時の表で読めない。firmware は今まで低い所に置く。実機の log の `FDT PASS phys=` で確かめる。
- R3: SD の card が bootloader の後に 1.8 V の signalling のまま残っていると、3.3 V の前提の初期化が通らない事がある。対策: 失敗したら aon_gpio4（SD の電源）で電源を入れ直し、aon_gpio3 を 3.3 V にしてやり直す（p002 の実機の結果を見て足す）。
- R4: mailbox の framebuffer の address が 1 GiB の上なら mask で壊れる。log の値で確かめる。
- R5: D0 の基板（2 GB 版など）の差は dtb で吸収するが、両方の実機は無いかもしれない。
