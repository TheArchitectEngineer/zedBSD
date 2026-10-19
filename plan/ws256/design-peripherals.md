# WS256 VisionFive 2 の周辺の設計（p012、改訂 4）

- Phase: [ws256-p012](phase012/phase.md)。版 1 は q1270-i01（2026-10-19、P1）、改訂 2 は q1270-i02（2026-10-19、P1、[レビュー 1](design-peripherals-review-1.md) の全部を反映、対応は §9）、改訂 3 は q1272-i01（2026-10-19、P2）: [レビュー 2](design-peripherals-review-2.md)（Blocker 1・Major 7・Minor 13）の全部を当てた（対応は §10）。**改訂 4 は q1278-i01（2026-10-19、P2）**: [レビュー 3](design-peripherals-review-3.md)（Blocker 0・Major 2・Minor 13）の全部を当てた。対応の表は §11。
- 範囲: p011（framebuffer）と p013〜p025 の設計。code は書かない。HAL の差分は案だけで適用しない（[proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)）。
- 前提: [design.md](design.md)（改訂 2）の HAL の芯・起動の道・ユーザーの決定 1〜12、p004 で入った loader → HAL の `bootloader/include/riscv64-handoff.h`（版 1、`version`・`size` を持ち、後の版は field を後ろに足す: ab23cbac7）。design.md の直しが要る所は §8 に Q1 への依頼として集めた（この文書からは直さない）。
- 改訂 3 はレビュー 3 を通った（結論「Major 1・2 を直す小さな改訂の後なら受け入れてよい」）。**改訂 4 はその小さな改訂で、Q1 の判定の前**（P2 は子のエージェントを起動しない）。
- **Phase の ID**: 新しい Phase は ws.md の次の番号 p020〜p025（Master の next は BUG・q・WS だけで、Phase の番号は ws.md が持つ。改訂 2 の「仮」は外した）。p020〜p023 は改訂 2 の物、p024（p017 の実機の部分）と p025（p005 の skeleton）は改訂 3 で分けた（§4.1）。

## 0. 出典

### 0.1 取得した物（2026-10-19、P1 の worktree の `build/tmp/ws256-docs/`、git に入れない）

| file | URL | SHA-256 | license・扱い |
| --- | --- | --- | --- |
| JH7110_TRM_StarFive_Preliminary_V2.pdf | https://doc-en.rvspace.org/JH7110/PDF/JH7110_TRM_StarFive_Preliminary_V2.pdf | 6ad0dcc58948a69daf74d17899d1e83bc05292523edaa613c0c883c590c88c92 | StarFive の公開の文書。memory map・割り込み・CRG・SYSCON・IOMUX・PMU の事実の第一の出典 |
| JH7110_Datasheet.pdf | https://doc-en.rvspace.org/JH7110/PDF/JH7110_Datasheet.pdf | 5d3eb13e93a97e819464d61343a81fbc286509f77cfcf64688d11d4c3217e80f | 同上。機能の一覧（対応 PHY の表など） |
| VisionFive2_Datasheet.pdf | https://doc-en.rvspace.org/VisionFive2/PDF/VisionFive2_Datasheet.pdf | d5bfbf769f4a3b7bf624657903393ac1e12a937c30f1e9c355cda07fe42215e0 | 同上。板の構成・40 pin・boot の switch |
| jh7110.dtsi | https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/riscv/boot/dts/starfive/jh7110.dtsi | 53dc95b66a6fd91cbe0e87f28b6eed295766be30d6146e4cb59e4070d3a289ab | GPL-2.0 OR MIT。値（address・IRQ・clock/reset の番号） |
| jh7110-common.dtsi | …/v6.12/arch/riscv/boot/dts/starfive/jh7110-common.dtsi | a1fd9f5571295c3e398257befa23b5c4bd72d597be7e9ccdcc03af31f240013c | 同上。VF2 の板の配線 |
| jh7110-starfive-visionfive-2.dtsi | …/jh7110-starfive-visionfive-2.dtsi | 4847a7997f914bdf6b124220da841ebdebe9c8d103de93e357e92eab9cff8253 | 同上 |
| jh7110-starfive-visionfive-2-v1.2a.dts / -v1.3b.dts | …/-v1.2a.dts、…/-v1.3b.dts | `SHA256SUMS`（fe9ee6a3…、d86cf0fc…） | 同上。版ごとの GbE の差 |
| jh7110-pinfunc.h、clock-jh7110-crg.h、reset-jh7110-crg.h | …/jh7110-pinfunc.h、…/include/dt-bindings/clock/starfive,jh7110-crg.h、…/include/dt-bindings/reset/starfive,jh7110-crg.h | `SHA256SUMS` | GPL-2.0 OR MIT（binding の header）。番号 |
| linux-v6.12-sifive_ccache.c | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/cache/sifive_ccache.c | 306f635a9e7bb7c6c6fee20d13327c0aac5f19269d05a6ffd4612d3debecb770 | GPL-2.0。読むだけ（§1.2） |
| linux-v6.12-arch-riscv-Kconfig | https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/riscv/Kconfig | f344838853cd008f9252d72a82b19a8e655d73d57431ae60fba71958f6e3c377 | GPL-2.0。20 行 `select ARCH_DMA_DEFAULT_COHERENT`（§1.2） |
| linux-v6.12-kernel-dma-Kconfig | https://raw.githubusercontent.com/torvalds/linux/v6.12/kernel/dma/Kconfig | 0bba904da9ea8dec2e32be7deff901ece9b6a5e67495c59a70953fe92890ce8b | GPL-2.0。85 行 `config ARCH_DMA_DEFAULT_COHERENT` |
| linux-v6.12-drivers-of-address.c | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/of/address.c | 56e3ebb0b3b2a822f0fbdab5b2488ffc91fafddd345fa595ce8e006dd9e17640 | GPL-2.0。`of_dma_is_coherent`（1001〜1019 行） |
| linux-v6.12-drivers-of-device.c（読むだけ） | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/of/device.c | cf8f77133cf15587976eeb4adf0d4116f1890fe93770ddc98da7c378d06cb2a8 | GPL-2.0 |
| obsd-sfcc.c | https://raw.githubusercontent.com/openbsd/src/master/sys/arch/riscv64/dev/sfcc.c | 637b04841d2bb980273c080ad1da547ebde309b969bdc309fa41d87955f6b2cd | ISC。62〜63 行で `sifive,fu540-c000-ccache` と `starfive,jh7100-ccache` だけに attach（jh7110 は無い） |
| u-boot-vf2-defconfig | https://raw.githubusercontent.com/starfive-tech/u-boot/JH7110_VisionFive2_devel/configs/starfive_visionfive2_defconfig | e051baa92489f53dddd0b5dfd4965fe0e0d0772985850b040b349f5266ed3272 | GPL-2.0+。配布の U-Boot の構成 |
| u-boot-mainline-vf2-defconfig | https://raw.githubusercontent.com/u-boot/u-boot/v2025.01/configs/starfive_visionfive2_defconfig | 25b00e347cc7c40265df192fd59095bda1c3219f35e9d1668e2e8c5eb7880f91 | 同上。mainline（44 行の PREBOOT、51 行 `PCI_INIT_R`） |
| README.md（StarFive の SDK） | https://raw.githubusercontent.com/starfive-tech/VisionFive2/JH7110_VisionFive2_devel/README.md | d9ef3d14363c59f4dbfba54a24fc4dbf881b8668c2987569cfbe4dc28a06ea1a | SDK の U-Boot の版（2021.10） |
| obsd-stfclock.c・stfpcie.c・stfpciephy.c・stfpinctrl.c（`sys/arch/riscv64/dev/`）、dwmmc.c・if_dwqe_fdt.c・axppmic.c（`sys/dev/fdt/`）、dwqe.c（`sys/dev/ic/`）、ytphy.c（`sys/dev/mii/`） | https://raw.githubusercontent.com/openbsd/src/master/<path>（master 859883f17cb8e20088fece5398547e4264ad30b3、2026-10-19T03:16:39Z。sfcc.c は同日の master） | `SHA256SUMS` | ISC。手順の参考。code は写さない |

- `SHA256SUMS` に全部。`starfive,jh7110-crg.h` は reset の header と同一の重複（片付けは Q1）。
- **改訂 3 で新しく取得した物は無い**（P2 は P1 の directory を読むだけ。`sha256sum -c SHA256SUMS` はレビュー 2 が失敗 0 を確かめた）。改訂 3 の新しい事実は zedBSD の tree（§1.5）と既に取得した物（jh7110.dtsi の ccache・pcie の node、mainline の defconfig の PREBOOT）から読んだ。

### 0.2 参照の扱い

- 形は [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」とユーザーの決定 6（「WS245 と同じ」）: 値は block ごとに `Value(s) referenced:`、手順は `Procedure referenced:` を block の直前の複数行の comment に書く。file の先頭には書かない。code・comment・構造・symbol の名前は写さない。Linux・U-Boot の source は読むだけで tree に入れない。
- **ユーザーの決定（2026-10-19 13:39、クリック）**: 手順の出典が Linux（GPL）しか無い部品（PWMDAC・DW AXI DMAC・DC8200/HDMI・YT8531 の拡張 register・dw_mmc の sample phase）は「手順の参考を許す」（i915 と同じく code・comment・構造は写さず、block ごとに Procedure/Values referenced:）。**この一覧に無い部品に GPL の手順の参考を広げない。**
- 部品ごとの出典の表（手順の順位は TRM → ISC（OpenBSD）→ GPL の手順の参考）:

| 部品 | register の値 | 手順 | GPL の手順の参考 |
| --- | --- | --- | --- |
| SYS/STG/AON CRG・SYSCON・IOMUX・PMU | TRM | OpenBSD stfclock.c・stfpinctrl.c | 不要 |
| DW MMC（基本） | OpenBSD dwmmc.c の値、Linux dw_mmc の値 | OpenBSD dwmmc.c（PIO・IDMAC とも） | 不要 |
| DW MMC の JH7110 の sample phase（`starfive,sysreg`） | Linux dw_mmc-starfive | **Linux**（OpenBSD の glue に tuning は無い） | **許す（決定）** |
| PLDA PCIe・PHY | OpenBSD stfpcie.c・stfpciephy.c | OpenBSD | 不要（MSI は使わない。後で MSI を足す時は出典の扱いを改めて聞く: Minor 3） |
| DW Ethernet QoS | OpenBSD dwqe.c・if_dwqe_fdt.c | OpenBSD | 不要 |
| YT8531 の拡張 register | Linux motorcomm.c | **Linux**（OpenBSD ytphy.c は YT8521 だけ） | **許す（決定）** |
| PWMDAC | Linux jh7110_pwmdac.c | **Linux** | **許す（決定）** |
| DW AXI DMAC | Linux dw-axi-dmac | **Linux** | **許す（決定）** |
| DC8200・HDMI TX（p011 (b)） | 配布の U-Boot sf_vop.c・sf_hdmi.c、vendor Linux | **GPL** | **許す（決定）** |

## 1. 確かめた事実

### 1.1 板の構成（VisionFive2_Datasheet、Linux v6.12 の DTS）

| 機能 | 事実 | 出典 | 未確認 |
| --- | --- | --- | --- |
| SD | TF card の slot。SDIO1（`mmc1`、0x16020000、`starfive,jh7110-mmc`、IRQ 75、4 bit、`cap-sd-highspeed`、`no-sdio`・`no-mmc`、card detect は sysgpio 41（active low）、biu = SYSCLK_SDIO1_AHB、ciu = SDCARD 50 MHz、`fifo-depth = 32`、`starfive,sysreg` 0x9c） | DTS、TRM | UHS 無し → 3.3 V の 50 MHz まで |
| eMMC | `mmc0`（0x16010000、8 bit、vqmmc は PMIC aldo4） | DTS | 範囲外 |
| SD の IP | DW Mobile Storage Host（U-Boot `MMC_DW`・`MMC_DW_SNPS`、OpenBSD dwmmc.c） | defconfig、OpenBSD | 公開の databook 無し。IDMAC の 32/64 bit は HCON の DMA64 で決まる（obsd-dwmmc.c:346）: 実機で HCON を読む |
| PCIe | 2 本の PCIe 2.0 x1（pcie0 cfg 0x9_4000_0000・apb 0x2b000000、pcie1 0x9_c000_0000・0x2c000000、DTS の cfg の reg は 16 MiB）。IP は PLDA。PHY pciephy0/1。**INTA〜D・MSI・event は 1 本の PLIC の線（pcie0 56、pcie1 57）にまとまり、host が `ISTATUS_LOCAL`（0x184）を読んで振り分け W1C で ack、`IMASK_LOCAL`（0x180、bit 24〜27 が INTA〜D、bit が 1 で許可）で pin ごとに許可**（obsd-stfpcie.c 53〜60・594〜641・693〜714 行）。`interrupt-map` は両方の host とも自分の子の `pcie_intc0/1` の **hwirq 1〜4** を指す（jh7110.dtsi:1237・1280。**2 本の host で同じ番号**）。**pcie0・pcie1 とも板で `okay`**（jh7110-starfive-visionfive-2.dtsi:36-42）。PERST sysgpio 26・28。窓 32 bit 0x30000000/0x38000000（各 128 MiB）、64 bit prefetch 0x9_0000_0000/0x9_8000_0000（各 1 GiB）。**`dma-ranges` は無い**（inbound の窓は DT に無い）。**STG SYSCON の offset（0x048・0x1f8 など）は DT に無く、controller の cfg の address で 0/1 を選ぶ**（obsd-stfpcie.c:271-281）。OpenBSD は outbound の ATR（`ATR_AXI4_SLV0_*`）だけを書き、inbound は書かない | DTS、TRM、OpenBSD | XpressRICH の名前は公開資料に無い。`ISTATUS_LOCAL` が latch か level か（ack が要る前提で設計）。inbound の窓の reset の値 |
| USB 3.0 ×4 | PCIe の先の xHCI（U-Boot `USB_XHCI_PCI`） | VF2 datasheet、defconfig | **VL805 の名前は公開資料に無い**。どちらの PCIe かも未確認（p020 の最初に読む） |
| SoC の USB | `usb0`（Cadence、peripheral） | DTS | 範囲外 |
| M.2 | M-Key、PCIe 2.0 x1 | VF2 datasheet | どちらの PCIe か未確認 |
| GbE | gmac0（0x16030000）・gmac1（0x16040000）、`snps,dwmac-5.20`、RGMII。gmac0 は `starfive,syscon = <&aon_syscon 0xc 0x12>`（**AON SYSCON**、jh7110.dtsi:1028）。v1.3B は `starfive,tx-use-rgmii-clk` を持つ → TX の clock の set_rate は不要（obsd-if_dwqe_fdt.c:284-286）。v1.2A の gmac1 は RMII | DTS | **PHY の型番**: VisionFive2_Datasheet に YT8531 の名は無い（grep 0 件）。JH7110 datasheet の対応 PHY の表（YT8521/YT8531）からの推定 → p018 の最初に MDIO で PHY の id を読んで決める |
| PMIC | AXP15060、I2C5（0x12050000）の 0x36。dcdc1 3.3 V always-on、dcdc2 CPU、aldo4 eMMC の vqmmc。PMIC を参照するのは mmc0 と cpu だけ | DTS | OpenSBI が電源断で I2C5 の PMIC を触る可能性（推測） |
| sound | 3.5 mm jack。**PWMDAC**（0x100b0000、`starfive,jh7110-pwmdac`）、`simple-audio-card`、codec は dummy。**PWMDAC の node に `interrupts` は無い**。`dmas = <&dma 22>` の 22 は **DMAC の handshake（request line）の番号**で、DMAC の channel は `dma-channels = <4>`（jh7110.dtsi:557-565・1063-1074） | DTS | PWMDAC の register は TRM に無い（Linux の値、§0.2） |
| HDMI | TRM の map に DC8200・`U0_HDMITX`（0x29590000）・VOUT_CRG。mainline v6.12 の DTS には display の node が無い | TRM、DTS | p011 |
| L2（ccache） | 0x2010000、`starfive,jh7110-ccache`・`sifive,ccache0`、**2 MiB、2048 set、64 byte の line → 16 way**（jh7110.dtsi:364-373）。FLUSH64（offset 0x200） | DTS | — |
| 40 pin の UART | pin 8 TX（GPIO5）、10 RX（GPIO6）、6・9 GND | VF2 datasheet §5.1 | — |
| boot の switch | RGPIO_1/0: 00 QSPI flash、01 SD、10 eMMC、11 UART（TRM Table 2-2 も同じ: 0x1 SDIO、0x2 eMMC）。**design.md §3.2 の「MSEL[1:0]: 10 SD」は誤り → §8 で Q1 に直しを頼む** | VF2 datasheet、TRM | 板の今の設定 |
| 再起動 | `gpio-restart`（sysgpio 35） | DTS | 電源断は OpenSBI の SRST（未確認） |

### 1.2 DMA の coherency（design.md §1.6 を直す）

- 事実（2026-10-19）:
  1. Linux v6.12 の `sifive_ccache.c` の非 coherent の cache 操作（FLUSH64）は `starfive,jh7100-ccache` だけに登録（123-124・318-322 行）。
  2. OpenBSD の `sfcc.c` も fu540 と jh7100 だけに attach する（62-63 行）。
  3. **Linux の riscv は `ARCH_DMA_DEFAULT_COHERENT` を select する**（arch/riscv/Kconfig:20）。`of_dma_is_coherent` は `dma-coherent`・`dma-noncoherent` が無い時 `dma_default_coherent` を返す（drivers/of/address.c:1001-1019）。→ riscv では「DTS に `dma-noncoherent` が無い」=「Linux は coherent と扱う」の意味を持つ。
  4. `jh7110.dtsi` に `dma-noncoherent` は 0 件。
  5. TRM Table 2-4 は DDR を 0x0_4000_0000〜（memory port、`RWX C A N`）と 0x4_4000_0000〜（system port、`RWX N`）の 2 窓で書く。**uncached の根拠は Table 2-5（U74 の map）の `0x04_4000_0000 RW A`（C 無し）**（TRM の text の 2207 行）。
- → **JH7110 の周辺の DMA は coherent と推定**。design.md §1.6 は JH7100 の取り違えと見る（§8 で直しを頼む）。**推定のまま。実機の確かめは §2.4（p023）。**
- **表示（DC8200）の coherency は別に扱う**（scan-out は CPU の書き込みを読むだけで、snoop が無ければ L2 に残った画が出ない）。§3.1。

### 1.3 割り込みの番号

- TRM の `global_interrupts[n]` と DTS は n + 5（UART0 27→32、PLDA 51→56、SDIO1 70→75）。driver は DTS の値だけを使う。

### 1.4 U-Boot と framebuffer

- 配布の U-Boot（2021.10）: `DM_VIDEO`・`VIDEO_STARFIVE`・`DISPLAY_STARFIVE_HDMI`・`VIDEO_COPY`、`drivers/video/starfive/`。PREBOOT は `run chipa_set_uboot;`（`usb start` は無い）。
- mainline の U-Boot v2025.01: VIDEO 無し。**PREBOOT は `nvme scan; usb start; setenv fdt_addr ${fdtcontroladdr}; fdt addr ${fdtcontroladdr};`、`PCI_INIT_R=y`**（defconfig 44・51 行。改訂 2 は `nvme scan` を書き落としていた）→ PLDA・VL805・NVMe は U-Boot が動かした状態で渡る（§3.5 p020・§3.11 p024 の試験）。OF_UPSTREAM で mainline の DTS から作った control DT を渡す。`CMD_CLK` は defconfig に無い（grep 0 件。Debian の build の構成は未確認）。
- → **GOP が出る見込みは配布の U-Boot だけ**（U-Boot の EFI は video の device があれば GOP を出す。VF2 で出すかは未確認）。ExitBootServices の後に画が残るか、GOP の framebuffer を U-Boot が EFI の memory map・DT の `/reserved-memory` でどう示すかも未確認 → **zedBSD は U-Boot の示し方に頼らず、loader が GOP の範囲を handoff の予約に入れる**（§3.1）。
- 配布の U-Boot は `RISCV_EFI_BOOT_PROTOCOL` を持たない可能性がある（追加は 2022 年、design.md:288）。**今の loader は protocol だけを使い、無ければ "Boot hart" の error で firmware に戻る**（`bootloader/uefi/bootriscv64.c:708-733` の `find_boot_hart`、`efi_main` の 169-173 行）。`/chosen/boot-hartid` の代わりの道も GOP の code も無い（riscv64 の loader の source は `bootriscv64.c`・`elf64.c`・`volume-discovery.c`・`zedbsd-config.c` だけ: `platform/riscv64/bootloader.mk:16-17`。FDT は header の検査と複写だけ: 770-773 行）。→ p011 (1) で loader に小さな変更を足す（§3.1 (1)、レビュー 3 Major 1）。

### 1.5 zedBSD の今の土台（改訂 3 で tree を読んで確かめた）

| 事実 | 所 |
| --- | --- |
| `hal_get_arch_handoff(const char *name)` は 1 引数で、無い・知らない名前は NULL を返す。返す物は HAL が持つ | include/hal/hal.h:1284-1292。riscv64 は src/hal/riscv64/bootinfo.c:236 |
| framebuffer の名前の単位は名前ごとに決める（`struct kern_boot_framebuffer` の comment）。rpi4 は pitch が byte、format が firmware の画素の順（0 青から、1 赤から）。pcat は pitch が pixel、format が `KERN_BOOT_FRAMEBUFFER_*` | include/kern/platform.h:80-88、src/hal/arm64/bsp-rpi4/boot.c:50-56、src/hal/amd64/bsp-pcat/boot.c:84-93 |
| `KERN_BOOT_FRAMEBUFFER_RGBX8888` = 1（赤から）、`BGRX8888` = 2（青から）。UEFI の loader の `video.c` はこの 2 つの GOP の形だけを受ける | include/kern/boot.h:56-61、bootloader/uefi/video.c:266-288 |
| riscv64 の loader の handoff（版 1）は `magic`・`version`・`size`・`flags`・boot hart・FDT・initrd・kernel の範囲・boot parameter だけ。framebuffer も memory map も無い。版を上げる時は field を後ろに足し、読み手は知っている版の field だけを取る | bootloader/include/riscv64-handoff.h |
| riscv64 の HAL の memory は DT の `/memory` から作り、`reserved[]`（allocator から外す）と `nomap[]`（allocator と direct map から外す）を各 16 個まで持つ | src/hal/riscv64/fdt.h:20・85-91、page.c:120-140、space.c:1111-1120 |
| 早期の text console（80x25 の grid、grid から描き直す、`hal_putc` が SBI の console と画面に出す）は bsp-virt にある（cons.c・font.c・framebuffer.c）。riscv64 の BSP の口は `riscv64_board_init`・`_putc`・`_describe` だけ | src/hal/riscv64/bsp-virt/、bsp.h |
| 汎用の kernel の text console は無い（`kern_boot_framebuffer` の読み手は pcat の graphics、rpi4 の GPU への受け渡し、i915・amdrdna・nvrtx だけ） | src/kern/platform/rpi4.c:202-221、src/drivers/platform/pcat/graphics/backend.c:1049 |
| `pci_intx_dispatch` は最後に必ず `kern_irq_send_eoi(acknowledge)` を呼ぶ。riscv64 の `hal_irq_send_eoi` は hart が持つ ack と違う token で `HAL_FATAL`、1 回目の EOI で ack を NONE に戻す | src/drivers/pci/pci.c:3605-3644、src/hal/riscv64/irq.c:197-213 |
| INTx の線（`struct pci_intx_line`: irq・dispatching・removing・handlers）は全体の list で `vector` の番号だけで引かれ、同じ番号があれば handler を足すだけで登録しない。線は host も pin も持たない | pci.c:177-188・3036-3108・3128-3145 |
| 線の最後の解除は `removing` を立て mask し、lock を外して `kern_irq_unregister`、成功なら drain 済みとして `kern_free(line)` | pci.c:3487-3589 |
| riscv64 の `hal_irq_unregister` は実行中の handler を待たない（arm64 も同じ）。core の「HAL removal is a checked drain barrier」の comment と合わない（**範囲外の観察**、§8） | src/hal/riscv64/irq.c:155-188 |
| DMA の `constraints.coherent` は driver か host が自分で決める（rpi4 の各 driver と pci-brcmstb.c:443 が 0、pci-pcat.c:97 が 1）。0 の時は `kern_pmem_map_uncached` の写像（無ければ ENOTSUP） | src/drivers/generic/dma.c:337-347・995-1020 |
| riscv64 の vmunix は `src/kern/uncached.c` を既に build する（HAL は今 UNSUPPORTED を返す） | platform/riscv64/vmunix.mk:67、src/hal/riscv64/page.c:370 |
| device の窓は物理 P < 64 GiB を `0xffff_ffe0_0000_0000 + P` に 2 MiB の leaf で写す（第 2 段の table は init で確保、root は不変） | src/hal/riscv64/defs.h:55-60、space.c:924-975 |

## 2. 共通の設計

### 2.1 driver の置き場所と境界

- **HAL**: design.md のとおり ccache・CPU・timer・PLIC・SBI の console。改訂 3 で **早期の text console を VF2 でも HAL が描く**（§3.1、#29）。周辺の clock・reset・pinctrl・GPIO・I2C は HAL に入れない。
- **driver**: `src/drivers/platform/visionfive2/`（板・SoC に固有）と、IP が汎用の物は `src/drivers/<種類>/`（`mmc/dw-mmc.c`、`ethernet/dw-eqos.c`、`ethernet/yt8531.c`、`pci/pci-plda.c`、`pci/pci-ecam-fdt.c`、`dma/dw-axi-dmac.c`）。DW I2C の FDT 版は PMIC を外すので作らない（#22）。
- **kernel の platform**: `src/kern/platform/visionfive2.c`（p025 が skeleton を作る）が device の init の順と root の選び方を持つ（rpi4.c の形）。

### 2.2 device tree と U-Boot

- **ユーザーの決定 2（DTB は U-Boot から）を保つ**。既定は **mainline の U-Boot（Debian の `u-boot-starfive` 2025.01 を SD に置く、design.md §3.2 の既定）の control DT**。これは mainline の DTS から作る物で、kernel が支える binding（v6.12）と同じ。U-Boot の fixup（`/chosen/boot-hartid`、`/memory`、OpenSBI の reserved-memory の複写、`local-mac-address`）がそのまま付く。
- **loader で DTB を差し替えない**（fixup が全部消える）。別の DTB が要る時（配布の U-Boot を使う時: p011 (1) と #20 (a)）は、U-Boot の `load mmc … ${fdt_addr_r} dtb/…` と `bootefi <img> ${fdt_addr_r}` で渡す。U-Boot は bootefi に渡された DTB にも fixup を当てる見込み（**推測**、p011 (1) で `/chosen` と reserved-memory を読んで確かめる）。
- 配布の U-Boot（2021.10）は、p011 (1) の結果と #20 の答えまで主の道にしない（§3.1）。
- clock と reset の番号は binding の header の値を Value(s) referenced で使う。

### 2.3 試験の道

- QEMU に JH7110 の machine は無い。JH7110 の固有の block は実機だけ。
- QEMU の virt で試せる物: PCIe の先の既存の driver（xHCI・usb-hid・NVMe）の riscv64 での動作（p016・p017、ECAM の FDT host、INTx だけ）、HAL の console を共通に移した後の virt の画面（p011 の回帰）。virt の DMA は coherent。
- host の試験（`plan/ws256/tests/`、短い C）: CRG の表の計算（PLL の値から rate）、DW MMC の command の組み立て、PLDA の ATR の窓の計算、**p021 の INTx の hook（core と host の型の約束、§3.5）**、**PLDA の割り込みの処理（EOI の回数と W1C の順）**、NVMe の INTx の処理、YT8531 の拡張 register の値、**console の cell の描画（pitch・format・flush の範囲）**。
- 実機の試験は T1/T2（AGENTS.md「試験の担当 T1」）。判定は §5（#15）。

### 2.4 coherency の確かめ（p023。MJ6 で手順を直した）

- **時期**: 最初の判定は **p023「coherency の確かめ」**（p022 = p013 の実機の確かめの後、DMA を使う device の Phase の前）。道具は最小の DW AXI DMAC の mem→mem の転送（p019 の DMAC の driver の先取り、cyclic は無し）。p014 の DW MMC は **PIO から始める**ので、p014・p015 は p023 を待たない。各 bus master の Phase（p014 の IDMAC、p020/p024 の xHCI・NVMe、p018 の GMAC、p019 の DMAC の cyclic）は、最初に同じ手順の短い版をその device で 1 回流す。表示は p011 で別に（§3.1）。
- **試験の形（Minor 7）**: 試験の kernel の config（`plan/ws256/tests/` の config.mk で `CONFIG_VF2_DMA_TEST=y`、製品の config には入らない）が `/dev/vf2-dmatest` の cdev（`include/kern/cdev.h` の既存の登録）を作り、read で 1 回の確かめを走らせて 1 行の答え（`DMATEST <bus master> PASS` / `FAIL run=<n> offset=<x> view=<cached|alias>`）を返す。判定は #15 (A) で shell から `cat /dev/vf2-dmatest` を打ち、その command の答えの文字列で判定する（serial の出力の答えの文字列で判定する例外、#15）。
- **大きさ**: buffer は各 **64 KiB**（src・dst、合計 128 KiB = L2 の 1/16。1 way は 2048 set × 64 byte = 128 KiB なので、各 set に載るのは 1 line ずつ）。**L2 から追い出されて DRAM に書き戻される事で偽の合格が出ない**大きさにする。手順の間に他の memory の大きな操作をしない（shell の command の中で連続して行う）。
- **手順**:
  - **別名の写し方（試験、レビュー 3 Major 2・Minor 12）**: 試験は自分の DMA の device（coherent = 1）から **2 MiB に揃えた連続の 2 MiB を 1 つ**取り（`drv_dma_alloc_coherent(device, 2 MiB, 2 MiB)`。取れなければ `DMATEST FAIL alloc`）、src・dst・逆向きの buffer を全部その中に置く（下の 5 の 8 回の位置も、64 KiB の組を 128 KiB ずつずらしてその中）。その 2 MiB の別名（物理 + 0x4_0000_0000）を MMIO の写像の既存の口（`hal_space_map_device`、別名は `/memory` の外なので device の窓の 2 MiB の leaf になる: space.c:916-975）で写す。**写る 2 MiB は試験が allocator から取った RAM の別名だけ**なので、OpenSBI（DDR の先頭、design.md:140）や予約の別名を含まない。窓の leaf は外れない（space.c:990 付近の unmap は何もしない）ので、**試験の後も別名の leaf は残る**（allocator の RAM の uncached の view で、cache の状態を持たないので害は無い。「試験の間だけ写す」は改訂 3 の誤り）。
  0. **陽性の対照（先に流す、2 段）**:
     - **0a. 別名が同じ DRAM である事（レビュー 3 Major 2）**: 範囲を FLUSH64 で捨て、cacheable で模様 A を書いて FLUSH64（DRAM へ書き出して捨てる）、**別名で読んで A** である事。次に別名で模様 B を書いて FLUSH64（投機の読みで載った clean の line を捨てる）、**cacheable で読んで B** である事。両方が合えば `ALIAS OK`。どちらかが違えば `ALIAS MISMATCH`（別名が同じ DRAM を指さない、または書き込みが届かない）。
     - **0b. stale を検出できる事**（0a が OK の時だけ）: dst を cacheable で読んで L2 に clean の line を載せ、別名から dst に違う模様を書き、cacheable で dst を読み直す。古い模様が読めれば `CONTROL OK`。新しい模様が読めたら（別名の書き込みが L2 を無効にした、または line が追い出された）`CONTROL INCONCLUSIVE`。
     - **INCONCLUSIVE・MISMATCH でも止めない**: 主の試験（1〜5）は cacheable の view を src の模様と比べるだけで、別名無しで非 coherent を検出できる。その時は**主の試験だけで判定し、別名は分類（3 の向きの切り分け）にも使わない**（食い違いは「非 coherent、向きは未分類」）。phase.md には対照の答えも並べて書く（INCONCLUSIVE で合格した時は「陽性の対照は示せていない」と添える）。**MISMATCH の時は予備 (i) と p011 の (α) を採れない**（どちらも別名に依る）ので、非 coherent と出たら (i) の代わり（例 FLUSH64 だけで保つ streaming の写像）を改めて設計し、Q1 に返す。
  1. **CPU→device（dirty の line）**: CPU が src に模様（address と回数の hash）を cacheable で書き、**cache を flush しない**。
  2. **device→CPU（stale の clean の line）**: CPU が dst を cacheable で全部読む（clean の line を載せる）。**直ちに** device に src → dst を転送させ、完了の後に CPU が dst を cacheable で読む。
  3. **判定**: cacheable で読んだ dst が src の模様と全部一致すれば、その回は合格（driver が `coherent = 1` で見る view そのもの）。食い違えば、（0a・0b が OK の時だけ）別名でも dst を読んで向きを分ける（別名の dst が src の古い DRAM の内容 → CPU→device が非 coherent。別名の dst が正しく cacheable が古い → device→CPU が非 coherent）。別名の読みは分類だけに使い、判定には使わない（coherent な機械でも device の書き込みが L2 に留まれば別名は古い DRAM を読む）。
  4. **逆向きの回**: device が書いた dst に CPU が cacheable で書き戻し、それを device がもう一度別の buffer へ転送して 3 と同じく見る。
  5. **offset を変えて 8 回**: 試験の 2 MiB の中で src・dst の組を 128 KiB ずつずらす（L2 の別の set・way の組を通す）。
  6. **後始末**: 試験の終わりに（失敗した時も）全部の buffer の範囲を FLUSH64 で捨ててから返す（陽性の対照と非 coherent の回は L2 に古い clean の line を残すので、そのまま allocator に返すと次の使い手が古い内容を読む）。
  7. 8 回とも合格なら coherent。1 byte でも食い違えば非 coherent として下の予備へ。結果は bus master ごとに phase.md に記録（SDIO・GMAC・PCIe（PLDA の inbound の先の xHCI・NVMe）・AXI DMAC・表示）。
  - **短い版（各 bus master の Phase の最初、レビュー 3 Minor 12）**: 上の 1〜3 と 6 を 1 回だけ（offset は 1 つ、逆向きは無し）、その device が実際に DMA する物で行う。src→dst の取り方: **AXI DMAC の cyclic（p019）** は mem→mem と同じ 2 つの buffer を cyclic の 1 周で、**SDIO の IDMAC（p014）** は scratch の partition に src を書いて dst に読み戻す、**xHCI（p020）** は event ring（device→CPU: 予め CPU が cacheable で読んだ ring に device が event を書く）と command ring（CPU→device: CPU が書いた No Op command が完了する事）、**NVMe（p024）** は scratch の LBA に src を書いて dst に読み戻す、**GMAC（p018）** は MAC の内部 loopback で TX の buffer を RX の buffer に受ける。答えは同じ cdev の形（`DMATEST <bus master> PASS` か `FAIL …`）。
- **`constraints.coherent` を決める所（MJ7、レビュー 3 Minor 5 で層を直した）**: 汎用の FDT の helper `drv_fdt_dma_coherent(fdt, node, board_default)`（`src/drivers/generic/fdt.c`・`include/drivers/generic/fdt.h`、driver の内部の API）が、node から根へ親を辿り（`drv_fdt_parent`、fdt.c:280）、最初に見つけた `dma-coherent` → 1、`dma-noncoherent` → 0、根まで無ければ `board_default` を返す（Linux の `of_dma_is_coherent` と同じく親を辿る: address.c:1010-1019。JH7100 の DT は soc の node に `dma-noncoherent` を置いた例）。**板の既定は platform が持つ**: `src/kern/platform/visionfive2.c` の `VF2_DMA_DEFAULT_COHERENT`（初めは 1 = 推定。p023 で非 coherent と出たら 0 に変える）を、各 glue の attach の引数（`vf2-mmc.c`・`vf2-ethernet.c`・`vf2-pwmdac.c` と、`pci-plda.c`・`dw-axi-dmac.c` の FDT の attach の引数の `dma_default_coherent`）で渡す。汎用の `pci-plda.c`・`dw-axi-dmac.c` は `src/drivers/platform/visionfive2/` を呼ばない（層を逆にしない）。glue は helper の答えを `drv_dma_constraints.coherent` に入れる（PLDA は `drv_pci_bus_create_root` に渡す root の DMA の device）。既存の `pci-ecam-fdt.c:160-162`（`dma-coherent` が無ければ 0、node だけ）は意味が逆で親も辿らないが、QEMU virt の既定のまま変えない（helper に `board_default` = 0 で置き換えられるが、p021・p016 の範囲の外。Future Work の候補）。0 なのに予備 (i) がまだ無い時は `drv_dma_alloc_coherent` が ENOTSUP で失敗し、driver の attach が失敗として見える（黙って壊れない）。
- **予備の案 (i)（非 coherent だった時、MJ7 で直した）**: VF2 の BSP の `hal_pmem_map_uncached(paddr, size, &vaddr)`（hal.h:653-680 の契約のまま）を次の形で実装する。
  1. 範囲の line を ccache の FLUSH64 で書き出して捨てる（`fence` → line ごとに FLUSH64 → `fence`）。
  2. **確保ごとに、別名を 4 KiB の粒度で写す（レビュー 3 Major 2）**: device の窓は 2 MiB の leaf でしか写さない（space.c:1367-1389 の `map_device_blocks`）ので、2 MiB の leaf では同じ 2 MiB の OpenSBI（DDR の先頭 0x4000_0000、design.md:140）や予約の別名まで写る。そこで **device の窓の別名の範囲（0x4_4000_0000〜0x6_3FFF_FFFF、窓の上限 64 GiB の中）に限って 4 KiB の段を足す**（`src/hal/riscv64/space.c` の内部の `map_alias_pages(paddr, size)`、hal.h 不変）: 別名の 2 MiB の block を初めて使う時に第 3 段の table を 1 枚 HAL の page から取り（取れなければ `HAL_ERR_NOMEM` を返し driver の確保が失敗する）、`device_tables[root][index]` をその table を指す entry にし、**確保した page の別名の entry だけ**を有効にする。`kernel_table_lock` の中で書き、`riscv64_tlb_flush_everywhere()`（今の `hal_space_map_device` と同じ: space.c:961-966）。MMIO の 2 MiB の leaf とは範囲が重ならない（JH7110 の MMIO は 0x4000_0000 の下と PCIe の 0x9_0000_0000〜。別名の範囲に MMIO の写像の要求が来たら EINVAL で断る）。返す address は `RISCV64_DEVICE_BASE + paddr + 0x4_0000_0000`。
     - 採らなかった案: 予約・nomap・OpenSBI と重ならない 2 MiB の block だけの専用の pool。`hal_pmem_map_uncached` の契約（hal.h:653-680）は kernel の allocator が渡した `paddr` を受けるので、別の pool では確保の道（`dma.c` の allocator）も変える事になり、pool の大きさを前もって決める必要もある。
  3. `hal_pmem_unmap_uncached(vaddr, size)` は物理を逆算して FLUSH64 で捨てる（direct map の投機の読みで載った clean の line を捨て、以後 direct map が RAM を読む）。別名の 4 KiB の entry は外さずに残す（その page は allocator の RAM なので、別名が残っても OpenSBI・予約には届かず、別名は cache の状態を持たないので害が無い。外さないので TLB の shootdown も要らない）。
  4. **写すのは allocator から渡した RAM の page の別名だけ**（2 の 4 KiB の段で保証する）。OpenSBI の領域・DT の予約の別名は写さない（PMP が別名の側を守るかは未確認なので、そもそも触れない）。DMA の確保に LR/SC・AMO を使わない事を driver の規則にする（uncached の別名での atomic の可否は未確認）。**前提は 0a の `ALIAS OK`**（MISMATCH なら (i) は採れない、上の 0）。
  - **design.md §2.5（「448〜509 は invalid」「uncached の窓は無い」）も §2.1 も変えない**（root の entry を足さない、init の後に root を書かない）。改訂 2 の「8 GiB を 1 GiB の leaf で 448〜509 に写して封じる」は取り下げた（§2.5 の M4「予約と OpenSBI の領域を map しない」を破り、2・4 GB の板では存在しない memory も写すため）。
  - riscv64 の共通の `hal_pmem_map_uncached` は今 UNSUPPORTED（page.c:370）なので、BSP に委ねる内部の口（`riscv64_board_map_uncached`、src/hal の中、hal.h 不変）を足す。virt は UNSUPPORTED のまま（virt は coherent）。
  - 予備を使う時の LW: 2〜3（p023 の中、4 KiB の段 1 を含む。HAL の BSP と space.c の実装の変更、承認不要の範囲）。

## 3. Phase ごとの設計

各 Phase: **既存**・**新規**・**HAL**・**DT**・**試験**・**依存**・**LW**（1 LW ≈ 15〜25 分、±50%）。

### 3.1 p011 framebuffer（MJ3・MJ4・MJ5 で作り直した）

#### (1) 調べ（実機、p005 の後）

- mainline の U-Boot では GOP は出ない見込み（§1.4）なので、**配布の U-Boot を SD に置いて試す**（flash は書き換えない、design.md #8）。DTB は §2.2 の `bootefi <img> <fdt>` で mainline の物を渡す。
- **loader の小さな変更（レビュー 3 Major 1、(1) の最初に code として書く）**: 今の loader は protocol が無いと最初の段で止まり、GOP も読まないので、下の「見る物」を観察できない。`bootriscv64.c` に次を足す（`riscv64-handoff.h` は版 1 のまま、kernel・HAL は変えない）。
  1. **boot hart の代わりの道**: `RISCV_EFI_BOOT_PROTOCOL` が無い（`LocateProtocol` が失敗）時は、EFI の configuration table の DT（今も複写している物）の `/chosen/boot-hartid`（u32 か u64）を読む。そのために DT の複写を `find_boot_hart` の前に移し、struct block を辿って `/chosen` の 1 つの property だけを読む最小の読み手を足す（FDT の header の検査は 770-773 行の物を使う）。どちらも無ければ今の "Boot hart" の error。**どちらを使ったかを console に出す**（`RV64 HART protocol <n>` / `RV64 HART chosen <n>`）。protocol と `/chosen` の両方がある時は protocol を使い、値が違えば両方を出す（記録だけ）。
  2. **GOP の答え**: `EFI_GRAPHICS_OUTPUT_PROTOCOL` を `LocateProtocol` で探し、**今の mode を読むだけ**（`SetMode` は呼ばない）。`Mode->Mode`・`MaxMode`・`HorizontalResolution`・`VerticalResolution`・`PixelFormat`・`PixelsPerScanLine`・`FrameBufferBase`・`FrameBufferSize` を console に 1 行で出す（無ければ `RV64 GOP none`）。handoff には入れない（それは (2) (a)）。
  - これは (2) (a) の GOP の取り込みの前倒しの一部で、(2) (a) は同じ関数を handoff に繋ぐだけになる。試験: host（`/chosen/boot-hartid` の読み手を、u32・u64・property 無し・`/chosen` 無し・壊れた struct block の 5 つの DT の blob で）、build（riscv64、warning 0）、**回帰（T1）: virt の `boot-test.sh` の riscv64-uboot**（QEMU の U-Boot `qemu-riscv64_smode` は protocol を持つので protocol の道。console の 2 行が増えるだけで画は変わらない）。
- 見る物（判定は画面の目視と #15 (A) の答え）: banner の版、HDMI の画、`bdinfo` の FB の行、loader の GOP の答え（mode・FrameBufferBase/Size・PixelFormat・PixelsPerScanLine）、**ExitBootServices の後に画が残るか**、`/chosen/boot-hartid` と `RISCV_EFI_BOOT_PROTOCOL` の有無、渡した DTB に `/memory`・reserved-memory の fixup が付いたか、EFI の memory map での framebuffer の型（記録だけ。zedBSD はこれに頼らない）。
- **分岐（MJ5）**:
  - **GOP が出て ExitBootServices の後も画が残った時**: 主の道をどちらにするかを **#20 でユーザーに聞く**（(a) 配布の U-Boot を主にし mainline の DTB を `bootefi` で渡す / (b) mainline の U-Boot を主のまま、画は (b) の自前の display の driver まで無し）。(a) なら下の code を書き、p015 の「画面で login」に届く。
  - **出ない時（画が消える時を含む）**: (b) 自前の display の driver（DC8200・HDMI TX・VOUT の PMU・voutcrg。GPL の手順の参考は決定 13:39 で許されている）。大きい: 未見積、別の設計（p011 (b)、design-reviewer に通してから Phase に分ける）。mainline の U-Boot を主にしたまま。

#### (2) code（#20 が (a) で、#29 の承認の後）

- **土台の選択（MJ3）: 描画は HAL に置く**（kernel の新しい console は作らない）。理由: (1) HAL_FATAL と初期の message が画面に出る（kernel の console では出ない）。(2) bsp-virt に 80x25 の console が既にあり、rpi4 も HAL が firmware の framebuffer に描く（guardrail の WS044 p006 の前例）。(3) kernel 側は `hal_putc` の teletype のまま（virt と同じ、design.md §5.1）で、console の sink の多重化が要らない。kernel 側の console を新規にする案は、HAL_FATAL が画面に出ず、pcat の graphics の backend が pcat に固有なので新しく書く量が大きい（LW +6〜10 の見込み）ので採らない。
- **(a) loader → HAL の handoff の版 2**（`bootloader/include/riscv64-handoff.h`、`ZBL6_RISCV64_HANDOFF_VERSION` を 2 に。版 1 の field は全部その場に残し、後ろに足す。HAL は `version ≥ 2` かつ `size` が版 2 の終わりまで届く時だけ読む）。**HAL の今の判定を直す（レビュー 3 Minor 1）**: `src/hal/riscv64/cmain.c:159-162` は `version < ZBL6_RISCV64_HANDOFF_VERSION` と `size < sizeof(*handoff)` で断るので、VERSION を 2 に上げて field を足すと版 1 の loader の handoff を HAL_FATAL にする（header の約束 `riscv64-handoff.h:30-37` に反し、#10 (c) の「vmunix だけを更新」で現に起こる）。判定を **`version < 1`** と **`size < offsetof(struct zbl6_riscv64_handoff, parameters) + sizeof(handoff->parameters)`（版 1 の終わり）** に変え、版 2 の field は `version ≥ 2 && size ≥ 版 2 の終わり` の時だけ読む。header に `ZBL6_RISCV64_HANDOFF_V1_SIZE`（3160）・`_V2_SIZE` を置き、`_Static_assert` で「版 1 の終わりが 3160」「全体が 8 の倍数」「全体が 1 page（4096）以下」（loader は 1 page に置く: bootriscv64.c:208-212。版 2 は約 3456 byte）を守る:

  ```c
  /* Version 2: the screen the firmware's GOP left on, all zero when none. */
  uint64_t framebuffer_phys;
  uint64_t framebuffer_size;	/* bytes, GOP FrameBufferSize */
  uint32_t framebuffer_width;	/* pixels */
  uint32_t framebuffer_height;	/* pixels */
  uint32_t framebuffer_pitch;	/* bytes: PixelsPerScanLine * 4 */
  uint32_t framebuffer_format;	/* KERN_BOOT_FRAMEBUFFER_* */

  /* Version 2: ranges of RAM the firmware still uses, kept out of the allocator. */
  uint32_t reserved_count;
  uint32_t reserved_flags;	/* ZBL6_RISCV64_RESERVED_OVERFLOW: more than the array held */
  struct { uint64_t phys; uint64_t size; } reserved[ZBL6_RISCV64_RESERVED_MAX];	/* 16 */
  ```

  - **loader**（`bootriscv64.c`）: GOP があれば今の mode を読み（mode は変えない）、`video.c` の形の検査（RGBX・BGRX だけ、`FrameBufferSize ≥ pitch × height`）に通った時だけ framebuffer の field を入れる。無い・通らない時は全部 0 で続ける（design.md §3.3）。`ExitBootServices` の直前の memory map から、DT の `/memory` の中にあって boot の後も firmware が使う型（`EfiReservedMemoryType`・`EfiRuntimeServicesCode/Data`・`EfiUnusableMemory`・`EfiACPI*`・`EfiPalCode`・`EfiPersistentMemory`）を、隣り合う物を合わせて `reserved[]` に入れる。**framebuffer の範囲も `reserved[]` に必ず 1 つ入れる**（U-Boot がどの型で示すかに頼らない）。**16 個の上限の判定の時期（レビュー 3 Minor 2）**: 最初の `GetMemoryMap`（bootriscv64.c:1001、まだ console を使える）の map で数え、超えたら「予約が多すぎる」を画面と console に出して止める。`ExitBootServices` の再試行の中（1014-1027 行、console を使えない）で取り直した map が 16 を超えた時は、入る分だけ入れて `reserved_flags` に `ZBL6_RISCV64_RESERVED_OVERFLOW`（bit 0）を立てて続け、HAL がそれを見て `HAL_FATAL`（「firmware の予約が多すぎる」、HAL は画面に出せる）にする（黙って落とさない）。`reserved_flags` の他の bit は 0、読み手は知らない bit を無視する。
  - これで design.md §3.4 (b)「memory は UEFI の map が正」の意図（firmware が使う範囲を kernel に渡さない）が、p004 の実装（DT の `/memory` から作る）の上で満たされる（§8 で design.md の書き方の直しを Q1 に頼む）。
- **(b) HAL が予約を外す**（`src/hal/riscv64/cmain.c`・`page.c`・`space.c`、hal.h 不変）: 版 2 の `reserved[]` は **DT の `info->reserved[]`（`RISCV64_FDT_MAX_RESERVED` = 16: fdt.h:20・85-91）とは別の配列 `firmware_reserved[16]`** に写し、allocator はその両方を外す（レビュー 3 Minor 2: UEFI の map の `EfiReservedMemoryType` は DT の `/reserved-memory` の写しになり二重に数えられる見込みなので、同じ配列だと正常な板でも上限に届きうる）。写す時に、DT の `reserved[]`・`nomap[]` の 1 つに丸ごと含まれる範囲は飛ばし、残りで隣り合う・重なる物を合わせる。**framebuffer の範囲は、描き方が (β) なら `reserved[]` だけ（direct map は描く窓なので残す）、(α) なら `nomap[]` にも足す**（allocator と direct map の両方から外し、同じ DDR を cacheable と uncached の 2 つの別名で見せない）。`firmware_reserved`・`nomap` の 16 個の上限を超える時と `ZBL6_RISCV64_RESERVED_OVERFLOW` の時は `HAL_FATAL`（黙って取りこぼさない）。framebuffer が DT の `/memory` の外（MMIO の framebuffer）なら予約は要らず、device の窓で写す。
- **(c) console を riscv64 の共通に移す**（`src/hal/riscv64/cons.c`・`font.c`・`fbcons.c`。bsp-virt の cons.c・font.c と framebuffer.c の cell の描画を移し、bsp-virt は ramfb の設定だけを残す）。board は framebuffer を `riscv64_fbcons_attach(&fb)`（virt の address・width・height・pitch（byte）・format・`flush(start, length)`（virt は NULL））で渡す。80x25 の grid を中央に描く（rpi4 の形。virt は 640x400 なので今と同じ位置で、boot-test.sh の読み取りは変わらない）。format は RGBX・BGRX の両方を描く（virt の ramfb は XR24 = BGRX）。
- **(d) 表示の coherency（DC8200 が L2 を snoop するかは不明）**:
  - (α) framebuffer の範囲を **system port の別名（物理 + 0x4_0000_0000）で device の窓に写して**描く（PMA が uncached。CPU の書き込みは直に DDR）。写す前に範囲を FLUSH64 で捨てる。cons.c は grid から描き直し framebuffer を読まないので、uncached の遅い読みは起きない。
    - **写す時期（レビュー 3 Minor 6）**: 今の boot table は低い 64 GiB を全部写す（locore.S:100-108）が、`riscv64_space_init` で kernel の root に切り替えた後は写した所しか届かない。(α) の別名は切り替えの前に `map_hal_devices`（space.c:1350-1365）で写す（ccache と同じ扱い）。写さないと切り替えの後の最初の描画で fault し、HAL_FATAL の表示も同じ所で fault する。(β) の FLUSH64 は ccache が既に `map_hal_devices` で写されるので問題ない。
    - **nomap の範囲の注意（レビュー 3 Minor 7）**: `hal_space_map_device` は nomap でも `/memory` の中なら direct map の address を返す（space.c:948-952、`riscv64_range_is_ram` は page.c:176-204 の `/memory` を見る）。(α) で framebuffer を nomap にすると、後の WS269・WS270 が `riscv64.framebuffer.phys` を写した時に写していない direct map の address を受け取る。**(α) を選ぶ時は同じ Phase で `riscv64_range_is_ram`・`riscv64_range_touches_ram` の判定から nomap を除く**（nomap の範囲は device の窓で写す。src/hal の実装の変更）。
  - (β) direct map（cacheable）で描き、描いた所ごとに `flush` で ccache の FLUSH64（`hal_dcache_clean_range` と同じ）。**費用（Minor 10）**: grid（文字の shadow）から描くので、1 cell は 16 行 × 32 byte（≤ 2 line）= 最大 32 回の FLUSH64、`fence` は 1 回の描画の前後に 1 回ずつ（line ごとではない）。scroll は 80x25 の領域の描き直しで 400 行 × 2560 byte ÷ 64 = 16,000 回の FLUSH64 と fence 2 回。1080p の全面を描くのは初めの黒の塗り 1 回だけ（約 13 万回）。
  - **既定は (β)**（rpi4 と同じ形）。(1) の試験の結果で選び直す。
  - **試験（p011 (2) の最初）**: flush 無しで描いた画が出るか（出れば DC8200 は coherent で flush を省ける）、(β) で出るか、(α) で出るか、の 3 つを試験の kernel の boot parameter（`vf2.fb=nocache|flush|alias`）で切り替えて HDMI の画で見る。
- **(e) 名前付きの handoff**（#29、[proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)）: VF2 の BSP が `riscv64.framebuffer.{phys,size,width,height,pitch,format}` を返す。p011 の kernel（`visionfive2.c`）は `kern_boot_framebuffer("riscv64.framebuffer", &fb)` で読んで範囲と値を検査するだけ（描かない）。使い手は後の WS269（PowerVR）・WS270（Keiland）の画面の受け渡し（rpi4.c:202-221 の GPU への受け渡しと同じ形）。
- **既存**: bsp-virt の console・font、loader の `video.c` の検査、page.c の `reserve_range`、space.c の `nomap`、pmem.c の `kern_boot_framebuffer`。**新規**: 上の (a)〜(e)。**HAL**: hal.h 不変。(c)・(d) と予約は `src/hal/` の実装の変更、(e) と「VF2 の HAL が画面に console を描く」は HAL の責務の追加で #29 の承認が要る。**DT**: 無し。
- **試験**: host（cell の描画を fake の framebuffer に: pitch が width × 4 より大きい時、RGBX・BGRX、flush の範囲が描いた行だけを覆う事。handoff 版 1・版 2 の読み分け、`reserved[]` の上限）。build（riscv64 の virt と vf2、warning 0）。**回帰（T1 に依頼）: virt の `boot-test.sh` の riscv64-virt と riscv64-uboot の画面**（console を共通に移しても画が変わらない事）。実機: 画面で login（#15 (B) か目視）、GOP の範囲が allocator に渡らない事（`/dev/vf2-dmatest` と同じ形の試験の cdev で予約の範囲を答えさせる）。
- **依存**: (1) は p005（実機の login）と #15（loader の変更の code と host の試験・build は板の前に書ける）。(2) は (1) の結果・#20 の答え・#29 の承認、**p004（handoff 版 1 の merge）の後に p011 が handoff を版 2 に広げる**。**LW**: (1) 2〜4（実機の調べ 1〜2 ＋ loader の変更 1〜2: レビュー 3 Major 1）、(2) 9〜12（(a) 2〜3、(b) 2〜3、(c)・(d) 3〜4、(e) 0.5〜1、試験 1）→ 計 11〜16。(b) の自前の display は含まない。

### 3.2 p013 周辺の基盤（code、板の前）と p022 実機の確かめ

- **方針**: U-Boot が残した状態に頼らず、各 driver が要る clock の gate と reset の解除を自分で行う（冪等）。pinmux は最初は読んで期待の値と照合し、違えば設定する（GPIO 41 の card detect、PERST の 26・28）。
- **参照されない clock を止めない**: driver が使わない clock・PD に触らない。特に VOUT の clock と PD_VOUT（止めると GOP の画が消える）、CPU・DDR・UART0 の clock。未使用の gate を落とす処理を書かない。
- **新規** `src/drivers/platform/visionfive2/`:
  - `vf2-crg.c`: SYS CRG（0x13020000）・STG CRG（0x10230000）・AON CRG（0x17000000）の gate・分周・mux と reset。API は「(CRG の phandle, 番号) → enable・disable・set_rate・get_rate」と「(phandle, reset の番号) → assert・deassert」。**rate は SYS SYSCON の PLL0〜2 の設定を読んで親の rate を計算する**（PLL は読むだけで変えない）。範囲は p014〜p024 が使う clock だけ。set_rate は SDCARD だけ（GMAC の TX は v1.3B の `starfive,tx-use-rgmii-clk` で不要、v1.2A で要れば p018 で足す）。
  - `vf2-pinctrl.c`: SYS IOMUX（0x13040000）と AON IOMUX（0x17020000）の GPIO と GPO/GPI の mux。GPIO の割り込みは使わない（card detect は poll）。
  - `vf2-syscon.c`: **SYS SYSCON**（SDIO の `starfive,sysreg`、PLL の読み取り）・**STG SYSCON**（PCIe の AXI の設定、offset は cfg の address で 0/1 を選ぶ）・**AON SYSCON**（gmac0 の `starfive,syscon = <&aon_syscon 0xc 0x12>`）の read-modify-write。
  - DMA の coherency（code はこの Phase）: 汎用の `drv_fdt_dma_coherent(fdt, node, board_default)`（`src/drivers/generic/fdt.c`）と、platform（`visionfive2.c`）の `VF2_DMA_DEFAULT_COHERENT`（§2.4、レビュー 3 Minor 5。改訂 3 の `vf2-dma.c` の `vf2_dma_coherent()` は取り下げ）。
  - **PMIC（AXP15060）と DW I2C の FDT 版は範囲から外す（既定、#22）**。OpenSBI が電源断に I2C5 を使う可能性（推測）とも競合しない。
- **既存**: `drv_fdt`（`drv_fdt_find_compatible` の `after`、`drv_fdt_translate`）、rpi4-gpio.c の書き方。**HAL**: 無し。**DT**: `clocks`・`resets`・`assigned-clock-rates`・`*-gpios`・`starfive,syscon`・`starfive,sysreg`・`dma-coherent`・`dma-noncoherent`。
- **試験（p013、code）**: host の試験（PLL と分周から rate、gate・reset の bit の位置、`drv_fdt_dma_coherent` の 5 通り: node の `dma-coherent`、node の `dma-noncoherent`、親の `dma-noncoherent`、根まで無く既定 1、既定 0）。build（riscv64 の vf2 と virt、warning 0）。
- **p022 実機の確かめ**（p005 の後、#15 (A) で判定）:
  - **clock の rate（Minor 8）**: mainline の defconfig に `CMD_CLK` は無いので `clk dump` は使わない。U-Boot の `md.l` で SYS SYSCON の PLL の register と CRG の該当の register を読み（`md.l 0x13030000 …`、`md.l 0x13020000 …`）、その値を host の試験の計算に入れた期待の rate と、kernel が計算した rate（試験の cdev `/dev/vf2-clktest` の答え）を照合する。DT の `assigned-clock-rates` の値とも合う事を見る。
  - GPIO 41 の card detect を挿抜で読む（shell の command の答え）、PERST の出力。
- **依存**: p013 は **p025（skeleton の merge）** と p012。p022 は p013・p005（実機の login）・#15。**LW**: p013 5〜8、p022 1〜2（実機の debug は別に 2〜4）。

### 3.3 p014 SD カード

- **新規** `src/drivers/mmc/dw-mmc.c`（DW Mobile Storage Host）と `vf2-mmc.c`（JH7110 の glue: biu/ciu の clock、reset、`starfive,sysreg` の sample phase。sample phase の手順は Linux の参考を許す、§0.2）。
- **最初は FIFO の PIO**（OpenBSD dwmmc.c が PIO を持つ。fifo-depth 32）。**IDMAC は後**（p014 の後半、遅ければ Future Work）。IDMAC の descriptor の 32/64 bit は **HCON の DMA64 bit で決める**（obsd-dwmmc.c:346）。32 bit なら `drv_dma_device_create` の address_bits を 32 にし、届くのは DDR の下 3 GiB（0x4000_0000〜0xFFFF_FFFF）だけ。IDMAC を入れる時は p023 の結果の後に、§2.4 の短い確かめを SD の scratch の partition で流す。coherent は `drv_fdt_dma_coherent(…, VF2_DMA_DEFAULT_COHERENT)`。
- **SD の protocol は dw-mmc の側に最小の SD の初期化を書く**（CMD0・8・ACMD41・2・3・9・7・ACMD6・CMD16、high speed の CMD6）。**rpi4-sdhci.c からの切り出しはしない（既定）**: rpi4-sdhci.c は PIO の driver で SD の protocol が SDHCI と絡み、arm64 の `DIRECT_BASE` を直に持ち（rpi4-sdhci.c:24・195）、Pi 4 は SD が root なので退行すると起動しない。共有化は Future Work（§7）。
- block の device として既存の MBR・GPT・FAT の層（`src/drivers/disklabel/`）に繋ぐ。速度は 50 MHz の 4 bit まで。
- **HAL**: 無し。**DT**: `starfive,jh7110-mmc` の `mmc1`（`mmc0` は無視）、`fifo-depth`、`cd-gpios`、`bus-width`、`max-frequency`、`starfive,sysreg`。
- **試験**: host（command の組み立て）。実機: card の識別、**scratch の partition（試験の image の最後に置く未使用の 64 MiB、image の build で確保）だけ**の raw の読み書きの一致、ESP の FAT を ls。
- **依存**: p013・p022。**p023 を待たない**（PIO）。**LW**: PIO 6〜9、IDMAC +2〜3。

### 3.4 p015 rootfs からの起動

- SD の image（ESP・root）で、kernel が root を SD の root partition から mount し、initrd 無しで init まで行く。
- **既存**: rpi4 の SD の root の道（`rpi4.c` の `KERN_DEV_SD` の root の選び方と `vfs.c`）。**新規**: visionfive2.c の root の選択（`KERN_DEV_SD` の形）。image の build（`plan/ws256/tests/` の config.mk、AGENTS.md の試験の image の作り方）。**HAL**: 無し。
- **受け入れの画面**: ws.md の「画面で login」。p011 の GOP が出ないか #20 が (b) なら p015 が止まるので、**UART の対話（#15 (A)）での受け入れを可にするか**をユーザーに聞く（#27）。既定: 可（画面は p011 が出た後に追って確かめる）。
- **試験**: 実機で 10 回の起動と login。**依存**: p014・#27（p011 は画面の受け入れの時だけ）。**LW**: 3〜5。

### 3.5 p016 PCIe（virt の ECAM）・p021 PCI core の host の INTx の hook・p020 PLDA

- **p016（QEMU virt）**: `src/drivers/pci/pci-ecam-fdt.c`（`pci-host-ecam-generic`: config は ECAM、BAR は DT の `ranges` から host が割り当て、INTx は `interrupt-map` から PLIC の番号）。virt の PLIC の INTx は線ごとに別の source なので、既存の PCI core の `establish_intx`（vector = PLIC の番号を `kern_irq_register`）のままで動く（hook は使わない）。pci-brcmstb.c の host の形（`describe`・`start`・`publish`、bus_ops）を使う。MSI は無し（virt に AIA 無し）。
  - **新規の繋ぎ**: 改訂 3 の時点で `riscv-virt.c` は ns16550 だけを繋いでいた。p016 で ECAM の host の登録（`src/drivers/platform/riscv-virt/virt-pci.c`、riscv-virt.c:30）、xHCI・usb-hid の登録、keyboard の input-bridge の繋ぎを足し済み（`pci-ecam-fdt.c` は入済み、p016 は cleared 2026-10-19、T1-723 の走り B: レビュー 3 Minor 9）。
  - **試験**: virt の `-device qemu-xhci -device usb-kbd`（T1、画面の判定。打鍵は QMP の `send-key`: tools の変更の担当は #28）。xHCI は INTx を許す（pci-xhci.c:6459-6464）ので attach する。
  - **依存**: p007 だけ（`-kernel` の直の起動、host が BAR を割り当てる）。**LW**: 4〜6。レビュー 2 で「今の設計のままでよい」。

#### p021 PCI core の host の INTx の hook（BL1・MJ1・MJ2 で作り直した）

- **問題**: PLDA は INTA〜D を 1 本の PLIC の線にまとめ、host が `ISTATUS_LOCAL` を読んで振り分けと W1C の ack をする。今の core は (1) vector をそのまま `kern_irq_register` に渡す、(2) 線を全体の list で vector の番号だけで引く、(3) 線の dispatch の最後に EOI を送る（§1.5）。PLDA の 2 本の host はどちらも hwirq 1〜4 を使い、PLIC の 1 回の claim の中で複数の pin を配るので、(2) で衝突し (3) で 2 回目の EOI が `HAL_FATAL` になる。
- **設計**（`include/drivers/pci/pci.h` と `src/drivers/pci/pci.c`、driver の内部の API で hal.h は変えない）:

  ```c
  /*
   * Runs every handler on one shared pin line and returns.  It sends no
   * end of interrupt: the host that called it ends its own interrupt once.
   */
  typedef void (*drv_pci_intx_run_t)(void *line);

  struct drv_pci_bus_ops {
  	/* ...the fields of today... */

  	/*
  	 * A host that demultiplexes its own pins sets all four, or none.
  	 * vector is the host's own pin number (PLDA: the intc hwirq 1..4).
  	 */
  	int (*intx_establish)(void *host, unsigned vector, drv_pci_intx_run_t run, void *line);
  	int (*intx_disestablish)(void *host, unsigned vector, drv_pci_intx_run_t run, void *line);
  	void (*intx_mask)(void *host, unsigned vector);
  	void (*intx_unmask)(void *host, unsigned vector);
  };
  ```

  1. **EOI を送らない入口（BL1）**: 今の `pci_intx_dispatch` の本体（handler を順に呼ぶ、`dispatching` の増減）を `intx_line_run(void *line)` に分ける。`pci_intx_dispatch` は「`intx_line_run(line)` → `kern_irq_send_eoi(acknowledge)`」になり、hook の無い host（amd64・Pi 4・virt）の道は変わらない。hook のある host には `intx_line_run` を渡す。型は ack を持たない `drv_pci_intx_run_t` なので、host が誤って EOI の token を渡す道が無い。
  2. **線の鍵を (host, vector) にする（MJ1）**: `struct pci_intx_line` に `const struct drv_pci_bus_ops *ops` と `void *host` を足す（hook の無い host では両方 NULL = 今の全体の番号の空間）。`find_intx_line(host, vector)` で引く。hook のある host の線は `ops`・`host`・`vector`（= pin）を持つので、解除と mask の時に host は pin を知っている（`private_data` には頼らない）。pcie0 と pcie1 の hwirq 1 は別の線になる。
  3. **呼び分け**: core が `kern_irq_register` + `kern_irq_unmask` を呼ぶ所（establish_intx）は hook があれば `intx_establish`（host が表に入れる。**pin は mask のまま**で、core が線を publish した後に `intx_unmask` を呼ぶ: hook の無い道の register → publish → unmask と同じ順。p021 の実装で決めた）、`kern_irq_mask`/`kern_irq_unmask(line->irq)` の所（pci.c の 3 か所と解除の失敗の戻し）は `intx_mask`/`intx_unmask`、`kern_irq_unregister` の所は `intx_disestablish`。小さい helper（`line_mask`・`line_unmask`）にまとめる。
  4. **lock の順と文脈（MJ2）**:
     - `intx_establish`・`intx_mask`・`intx_unmask` は core の `intx_lock` を持ち割り込みを止めた spin の文脈で呼ばれる（今の `kern_irq_register` と同じ）。**眠らない、確保しない**。host が取ってよいのは自分の spin lock だけで、**順は `intx_lock` → host の lock だけ**。host の表は host の struct の中の固定の 4 要素（確保しない）。
     - `intx_disestablish` は core が `intx_lock` を外した後（今の `kern_irq_unregister` の位置）、thread の文脈で呼ばれる。**host の lock は全部の所で割り込みを止めて取る（irq-save、レビュー 3 Minor 3）**: `intx_disestablish` が thread の文脈で host の lock を持つ間に、同じ hart で host の ISR（PLIC の source は全部 boot hart が受ける: irq.c:16-17）が同じ lock を待つと戻らない。establish・mask・unmask は既に割り込みを止めた文脈だが、同じ irq-save の lock の helper を使う（入れ子でも正しい）。
     - **host の割り込みの処理は host の lock を持ったまま `run` を呼ばない**: lock の中で pin の表の要素を読み、`in_flight` を 1 足し、lock を外して `run(line)`、lock を取って `in_flight` を 1 引く。`run` の中で core が `intx_lock` を取るので、これで host の lock → `intx_lock` の順は生まれない（ABBA にならない）。
  5. **drain と戻り値（MJ2）**:
     - `intx_disestablish` は host の lock の中で要素が (run, line) と一致する事を確かめ（違えば EINVAL）、pin を mask し要素を空にする（以後の割り込みはこの pin を配らない）。lock を外して **その pin の `in_flight` が 0 になるまで待つ**（上限 1 秒、timer で測る）。0 になれば成功 0。上限を超えたら（例 別の hart の ISR がその pin の `run` の中で handler を戻さない。handler の中からの解除は core が `dispatching` で先に EBUSY を返すので host には届かない: pci.c:3528-3533）要素を元に戻して **EBUSY**（状態は呼ぶ前のまま、再試行できる）。core は EBUSY で今の失敗の道（`removing` を 0 に戻し `intx_unmask`）を通る。
     - `intx_establish` の戻り値: 0、EBUSY（その pin の要素が別の線で埋まっている。鍵が一意なので起きない筈の防御）、EINVAL（vector が 1〜4 の外）。core は今の `kern_irq_register` と同じく EBUSY 以外を EIO にして返す。
     - 成功の後は、どの hart も `run(line)` を実行中でなく、これからも呼ばないので、core は今のとおり `kern_free(line)` してよい。
  6. **host の割り込みの処理（EOI の約束）**: **pin と bit の対応（レビュー 3 Minor 11）**: vector v（1〜4、intc の hwirq）は `ISTATUS_LOCAL`・`IMASK_LOCAL` の bit `24 + (v − 1)`（INTA〜D: `IMASK_INT_INTA << (v − 1)`、obsd-stfpcie.c:53-57・594-641）。**配るのは `status & 許可した pin の bit` だけ**（`ISTATUS_LOCAL` が mask の外の bit も示すかは未確認なので、core が `intx_mask` した pin や空の要素の pin を配らない）。 host は attach の時に自分の PLIC の線（56・57）を 1 回だけ `kern_irq_register` し（core はその線に触らない）、`IMASK_LOCAL` を 0、`ISTATUS_LOCAL` を全部 W1C してから `kern_irq_unmask` する。handler は `status = ISTATUS_LOCAL` を読み、立った INTx の pin ごとに 4 の手順で `run` を呼び、**全部の後に** `status` を W1C（level の INTx を handler の後に ack: OpenBSD の順と同じ）、**最後に `kern_irq_send_eoi(acknowledge)` を 1 回だけ**送る。status が 0、event・MSI の bit だけ、の時も W1C と 1 回の EOI をして戻る（PLIC の claim を必ず終える）。MSI と event は許可しない（`IMASK_LOCAL` の bit 28 以上は 0 のまま。OpenBSD は MSI を許可するが zedBSD は MSI を使わない）。
  - hook が NULL の host（pci-brcmstb.c、amd64 の host、pci-ecam-fdt.c）は今の道のまま（`ops->intx_establish != NULL` の分岐と、鍵の host が NULL）。designated initializer なので足した field は NULL になる（pci-pcat.c:61、pci-brcmstb.c:233）。子の bus は `ops`・`host` を親から継ぐ（pci.c:330-331）ので root port の下の device にも届く。
- **試験**: host の試験（`plan/ws256/tests/pci-intx-hook/`、`plan/ws004/tests/run-pci-msi-test.sh` と同じく pci.c を host で compile し、fake の host を設計どおりに書く）:
  1. **2 本が同時**: status に INTA と INTB → 両方の handler が 1 回ずつ、W1C は両方の後に 1 回、EOI は 1 回。
  2. **event だけ**: status に event の bit だけ → handler は呼ばれず、W1C と EOI が 1 回。status 0 も同じ。
  3. **2 つの host・同じ hwirq**: host A と B の vector 1 に別の device → 線が 2 本、`intx_establish` が各 host で 1 回ずつ、A の割り込みで B の handler が走らない、B の解除が A の線に触らない。
  4. **drain（レビュー 3 Minor 4 で直した）**: core は `run` の中に handler が居る間（`line->dispatching != 0`）の解除を host に届く前に EBUSY で返す（pci.c:3528-3533）ので、「handler の中からの解除」では host の待ちを試せない。host の drain は、**fake の host の ISR を `in_flight++` と `run` の間で止めて**（試験の hook: pthread の barrier で ISR の thread を止める）試す。(a) 止めている間の `intx_disestablish` は待ち、ISR を進めて `run` → `in_flight--` の後に 0 で戻る（要素は空、pin は mask）。(b) 止めたまま上限を過ぎると EBUSY（fake の timer で 1 秒を進める）、要素と pin の許可が呼ぶ前のまま、core の線も `removing` が 0 に戻り `intx_unmask` されている。(c) handler の中からの解除は core の EBUSY（host の `intx_disestablish` は呼ばれない）。
  5. **lock の順**: fake の `run` の中で host の lock が空いている事を assert。
  6. **hook の無い道**: 今の `kern_irq_register(vector)` と EOI が 1 回である事（amd64・Pi 4 と同じ道）。
  - **試験の script（レビュー 3 Minor 4）**: 新しい runner は `run-pci-msi-test.sh` の `mktemp`＋`rm -rf` の trap を写さない（AGENTS.md「試験の script の中の host の rm は禁止」）。出力は `plan/tools/fresh-out.sh` で作った `build/tmp/` の下の directory に置き、片付けは Q1 の `plan/tools/q1-clean.sh`。
  - **既存の host の試験**: `plan/ws004/tests/pci-shared-intx-test.c` など pci.c を host で compile する試験が p021 の変更で壊れた時は、AGENTS.md の試験の整理の基準で決める: Master の Tools・試験の一覧、complete でない WS の未完了の Phase、tests/ のシナリオ、T1 の未実行の依頼のどれかから参照されていれば直し、どこからも参照されていなければ直さずに削除を Q1 に頼む（文書の参照も外す）。
  - build: amd64（pcat）・arm64（Pi 4）・riscv64（virt・vf2）の vmunix を warning 0。**回帰（T1 に依頼）: Pi 4 の xHCI（brcmstb の INTx の道。USB keyboard で login、boot-test.sh の画面）、amd64 の QEMU の xHCI と NVMe、riscv64 virt の `-device qemu-xhci -device usb-kbd`（T1-723 の走り B と同じ。EOI の検査が厳しいのは riscv64 だけで 2 回目の EOI は HAL_FATAL なので、`pci_intx_dispatch` の分け方の回帰に要る: レビュー 3 Minor 10）**。
- **依存**: p012 だけ（板の前に書ける）。**LW**: 5〜7（T1 の時間は別。改訂 2 の 3〜5 から、鍵・drain・試験の分を足した）。

#### p020 PLDA（実機）

- `src/drivers/pci/pci-plda.c`（apb の register、ATR の outbound（cfg・32 bit・64 bit）、link の train、PERST の GPIO、PHY（stfpciephy: mode の設定）、**INTx は p021 の hook で（上の 6 の handler）**、STG SYSCON は cfg の address で 0/1 を選ぶ）。手順は OpenBSD stfpcie.c・stfpciephy.c、値は Value(s) referenced。
  - **`allocate_irqs`（Minor 9）**: device の `PCI_INTERRUPT_PIN` を読み、root の bus までの各 bridge の下で slot の番号で swizzle し（pci-brcmstb.c:1505-1578 と同じ形。root port の下の endpoint が普通の形）、`interrupt-map` で自分の intc の hwirq（1〜4）にして `irq->vector` に入れる。
  - **DMA の address の bit 数と inbound の窓（Minor 11）**: DT に `dma-ranges` が無く、OpenBSD も inbound の ATR を書かない（§1.1）。attach の時に PCIe → AXI の ATR の register（PCIe の master の側の窓）を読んで答えに出す。**既定は root の DMA の device を 32 bit（DDR の下 3 GiB、0x4000_0000〜0xFFFF_FFFF）で作る**（`drv_pci_bus_create_root` に渡す dma device の address_bits = 32、coherent は attach の引数の板の既定を `drv_fdt_dma_coherent` に渡した答え）。読んだ窓が DDR 全体を 1:1 で覆うと確かめたら、p020 の中で 64 bit に広げる（xHCI の AC64・NVMe の 64 bit は各 driver が自分の制約で狭める）。
  - **U-Boot が動かした状態（Minor 12）**: mainline の PREBOOT の `nvme scan; usb start` と `PCI_INIT_R` で、PLDA・VL805・NVMe は動いた状態で渡る。attach の最初に PERST を assert して 100 ms 待ち、link を学習し直す。その前に xHCI の `USBSTS.HCHalted` と NVMe の `CC.EN`・`CSTS.RDY` を読んで答えに出し、ExitBootServices で U-Boot が device を止めたかを記録する（止まっていなくても PERST で落とす）。
  - **MSI は使わない**（§3.6）。PLDA の MSI の demux は Future Work（§7）。
  - USB: 既存の pci-xhci・usb-hub・usb-hid・hid-input をそのまま（INTx で attach）。VL805 なら firmware の load は無い見込み（未確認）。
  - **HAL**: 無し。**DT**: `starfive,jh7110-pcie`（reg の cfg・apb、ranges、interrupts、interrupt-map、`perst-gpios`、`phys`、`starfive,stg-syscon`、clocks・resets）。
  - **試験**: host（ATR の窓の計算、swizzle、割り込みの handler を fake の MMIO で: 2 本が同時・event だけ・status 0 で EOI が 1 回、W1C が handler の後）。実機: link up、列挙の答え（vendor・device、どちらの host か）、HCHalted・CC.EN の答え、USB keyboard で login。最初に §2.4 の短い確かめを xHCI で 1 回。
  - **依存**: p013・p022・p021・p016（ECAM で host の形を確かめた後）・p023。**LW**: 8〜12。

### 3.6 p017 NVMe の INTx の道（virt）

- **選択（B2）: pci-nvme に INTx の道を足す**（PLDA の MSI を host hook で出す案は選ばない）。理由: (1) virt の p016 も INTx だけなので、MSI の案では virt で NVMe が試せない。(2) PLDA の MSI は OpenBSD も `#ifdef notyet`、Pi 4 の brcmstb も MSI の受け手が無い（pci-brcmstb.c:1510）。(3) MSI の hook は PCI core の `kern_irq_register_msi` の道（pci.c:3209）を host に渡す大きな変更になる。
- **済み（pci-nvme.c、Minor 1 で直した形で main に入った: レビュー 3 Minor 9）**: `drv_pci_device_allocate_irqs` の許可に INTx を足した（pci-nvme.c:1743-1744。MSI-X・MSI が取れる時はそちらが先）。**INTx の mask の解除は既存の行が覆う**（enable の後、MSI-X 以外なら `INTMC = 1`: pci-nvme.c:5557-5558）。**ISR で INTMS → INTMC の往復はしない**（別の hart の `nvme_controller_disable` が INTMS で全部を mask した後に、ISR の INTMC が mask を外し直す競合ができるため: 2979-2980 行）。ISR は CQ を処理し、自分の CQ に何も無ければ「自分の割り込みでない」を返す（共有の level の線）。NVMe の INTx は host が CQ の head の doorbell を進めると下がる。どうしても往復が要ると分かった時は `command_lock` の中で `controller_enabled` を見てから行う。
- **試験**: host（INTx の時に INTMS/INTMC を ISR が書かない事、disable の後に INTMC が書かれない事）。QEMU virt（`-device nvme`、p016 の上、INTx）で読み書きと mount。**回帰（T1 に依頼）: amd64 の QEMU の NVMe（MSI-X の道が変わらない事）と、arm64 の build**。
- **依存**: p016。**LW**: 3〜4。レビュー 2 で「Minor 1 を当てれば進めてよい」。code は main に入済み。T1-723 の 1 は FAIL（nvme0n1 は出るが mkfs が Invalid argument、mount が Operation not supported で I/O は未確認）で、P1 が q1274 で試験の手順と I/O を確かめる（ws.md の p017 の行）。

### 3.7 p018 GbE と PHY

- **新規** `dw-eqos.c`（Ethernet QoS 5.20: DMA の descriptor の ring、MTL、MAC、MDIO）、`vf2-ethernet.c`（AHB・AXI・PTP・TX・RX の clock、RGMII の mode の **AON SYSCON** の bit、reset、coherent は `drv_fdt_dma_coherent(…, VF2_DMA_DEFAULT_COHERENT)`。TX の set_rate は `starfive,tx-use-rgmii-clk` が無い時だけ）、`yt8531.c`（**p018 の最初に MDIO で PHY の id を読んで型を決める**。拡張 register の RGMII の delay と `motorcomm,*`。手順は Linux の参考を許す、§0.2）。
- **技術の既定: gmac0 だけ**（v1.2A の gmac1 は RMII。板の版は決定 2026-10-18 17:46 で「つないだ時に記録」なので、つないだ時の版で gmac1 の要否を見直す）。
- **HAL**: 無し。**DT**: `starfive,jh7110-dwmac`、`phy-handle`・`phy-mode`・`mdio`、`rx/tx-internal-delay-ps`、`motorcomm,*`、`starfive,tx-use-rgmii-clk`、`starfive,syscon`、`assigned-clock-parents`。
- **試験**: 最初に §2.4 の短い確かめを GMAC の descriptor で 1 回。実機: link up、DHCP、host から ssh（§5 の (C)）。
- **依存**: p013・p022・p023。**LW**: 10〜14。

### 3.8 p019 sound

- **道（既定、#23）: 3.5 mm の PWMDAC**。
- **新規** `vf2-pwmdac.c` と `src/drivers/dma/dw-axi-dmac.c`（cyclic の転送。mem→mem の最小の部分は p023 で先に作る）。**PWMDAC の node に `interrupts` が無いので PIO は timer の poll になり、DMA は事実上必須**。DMAC の 4 本の channel から 1 本を取り、PWMDAC の handshake 22 を割り当てる。手順は Linux の参考を許す（§0.2）。
- 既存の `src/drivers/audio/audio.c`（`drv_audio_register`）に繋ぐ。48 kHz を既定。
- **HAL**: 無し。**DT**: `starfive,jh7110-pwmdac`、`dmas`、`starfive,jh7110-axi-dma`（`dma-channels`）、clocks・resets、`pinctrl-0`（GPO 28・29 を GPIO 33・34）。
- **試験**: 最初に §2.4 の短い確かめを DMAC の cyclic で 1 回。判定は #24。
- **依存**: p013・p022・p023。**LW**: 7〜11（JH7110 の DMAC に固有の差は推測で、上限寄り）。

### 3.9 p023 coherency の確かめ

- §2.4 の手順（陽性の対照 → 64 KiB の 8 回 → 逆向き）を、最小の DW AXI DMAC の mem→mem の転送（`dw-axi-dmac.c` の先取り）と試験の cdev `/dev/vf2-dmatest` で流す。結果で `VF2_DMA_DEFAULT_COHERENT` を決め、非 coherent なら同じ Phase で予備 (i)（確保ごとの別名の写像＋FLUSH64、HAL の BSP の実装の変更、hal.h・design.md §2.5 不変）を実装する。
- **依存**: p013・p022・#15。**LW**: 3〜4（(i) を実装する時 +2〜3）。

### 3.10 p025 VF2 の skeleton（p005 から分けた、Minor 6）

- p013・p021 の後の Phase が板の前に依る「skeleton」を、p005 の実機の部分から分けて名指す。
- **新規**: `src/kern/platform/visionfive2.c` の最小（console は `hal_putc` の teletype、device の表は空、root の選び方は initrd だけ）、`platform/riscv64/vmunix.mk` の vf2 の枝（object の一覧の置き場）、config と menuconfig の vf2 の項目、`src/hal/riscv64/bsp-visionfive2/` の最小の board。**BSP の口は `riscv64_board_init`・`_putc`・`_describe` の 3 つの他に `hal_dcache_clean_range`・`hal_dcache_invalidate_range`・`hal_dcache_clean_invalidate_range` がある**（bsp-virt/board.c:85-138。`src/kern/dcache.c:26・38` が呼ぶので無いと link しない: レビュー 3 Minor 8）。p025 ではこの 3 つを **ccache の FLUSH64 で実装する**（`fence` → 64 byte の line ごとに物理 address を FLUSH64（ccache 0x2010000 + 0x200）に書く → `fence`。受け取る address は kernel の仮想 address なので、direct map の範囲なら物理に戻し、それ以外（device の窓など）は何もしない。clean も invalidate も FLUSH64 が書き出して捨てるので同じ。coherent と推定する間は呼ばれる所が少ないが、空の stub にすると非 coherent だった時に黙って壊れる）。ccache の base は DT の `sifive,ccache0` から（今の `fdt.c` が `ccache_base` を読む: space.c:1363）。mimpid・marchid の log と errata は p005。
- **試験**: build（riscv64 の vf2 と virt、warning 0）。実機・QEMU は無し（vf2 の vmunix は virt では起動しない）。
- **依存**: p004（`platform/riscv64/` の mk を p004 が変えるので、その merge の後）。**LW**: 2〜3（design.md の p005 の 8〜12 から移す。p005 は 6〜9 ＋ debug 4〜8 になる）。
- p005 は p025 の後の、bsp-visionfive2 の中身（ccache の dcache、mimpid・marchid の log、errata）・DW UART の RX の癖・SD の 4 partition の image・実機の login（10 回）になる（design.md §6 の p005 の残り）。

### 3.11 p024 NVMe の実機と NVMe の root（p017 から分けた、Minor 6）

- p017 が virt の部分だけを q1271 で進めているので、実機の部分を別の Phase にする（p013/p022 と同じ分け方）。
- **新規（visionfive2.c）**: NVMe の root は rpi4 の道（`KERN_DEV_SD` だけ、`include/kern/boot.h` に NVMe の class は無い）ではなく **pcat の形**: `boot0=UUID=` の読み取り、`probe_fixed_disks`、`wait_for_boot_disk`（pcat.c:143・313・792-797・865）と `drv_pci_nvme_driver_register`・`drv_pci_nvme_probe_namespaces` の呼び出しを visionfive2.c に足す。
- **試験**: 実機: M.2 の SSD の識別・読み書きの一致・NVMe の root で起動。最初に §2.4 の短い確かめを NVMe で 1 回。U-Boot の `nvme scan` で動いた状態の NVMe を、p020 の PERST の後に attach できる事（`CC.EN` の答え）。
- **依存**: p017・p020・p015（root の選び方の土台）。**LW**: 3〜4（実機の debug は別）。

## 4. 依存と並行

### 4.1 Phase の一覧

| ID | 中身 | 依存 | LW |
| --- | --- | --- | --- |
| p011 | framebuffer: (1) loader の boot hart の代わりの道と GOP の答え・調べ、(2) handoff 版 2・HAL の予約・console の共通化・名前付きの handoff | (1) p005・#15、(2) (1) の結果・#20・#29・p004 | 11〜16 |
| p013 | 周辺の基盤の code（板の前） | p025・p012 | 5〜8 |
| p014 | SD（PIO、後で IDMAC） | p013・p022 | 6〜12 |
| p015 | SD の root で起動 | p014・#27 | 3〜5 |
| p016 | virt の ECAM の FDT host と virt の繋ぎ | p007 | 4〜6 |
| p017 | NVMe の INTx の道（virt） | p016 | 3〜4 |
| p018 | GbE と PHY | p013・p022・p023 | 10〜14 |
| p019 | sound（PWMDAC と DMAC の cyclic） | p013・p022・p023 | 7〜11 |
| p020 | PLDA の host（実機） | p013・p022・p021・p016・p023 | 8〜12 |
| p021 | PCI core の host の INTx の hook | p012 | 5〜7 |
| p022 | p013 の実機の確かめ | p013・p005・#15 | 1〜2 |
| p023 | coherency の確かめ（要れば予備 (i)） | p013・p022・#15 | 3〜7 |
| p024 | NVMe の実機と NVMe の root | p017・p020・p015 | 3〜4 |
| p025 | VF2 の skeleton（p005 から分けた） | p004 | 2〜3（p005 から移す） |

- 合計（p011・p013〜p024）: **69〜108 LW**（実機の debug は別に p022・p020・p018 で各 2〜8）。改訂 2 の 59〜96 から、p011 の土台（+7〜8）と p021 の鍵・drain（+2）を足し（改訂 3 の 68〜106）、改訂 4 で p011 (1) の loader の変更（+1〜2）を足した。p023 の別名の 4 KiB の段（予備 (i) の時だけ、§2.4）は p023 の LW の幅（3〜7）の中。p017 と p024 の和は改訂 2 の p017 と同じ。p025 の 2〜3 は p005 から移した分で、WS の合計は変わらない。p011 (b)（自前の display）は含まない。
- **判定の前提（Minor 7）**: #15 (A)（serial の対話）が p005・p022・p023・p014 の判定の前提。(A) が却下されると、(B)（HDMI の capture）は p011 で画が出た後、(C)（SSH）は p018 の後だが p018 は p023 に、p023 は判定に依るので (C) は最初の方法になれない（循環）。→ #15 の答えを p005 の前に得る。(A) と (D) が却下なら p022・p023・p014 は p011 の画（#20 (a)）を待つ。

```
p004 ─┬→ p025 ─┬→ p013(code) ─┐
      │        └→ p005(実機) ──┼→ p022(実機) ─┬→ p014 → p015 ───────────────┐
      │              │         │              └→ p023 ─┬→ p018               │
      │              └→ p011(1) → #20 → p011(2)        ├→ p019               │
      └─────────────────────────→ p011(2)              │                     │
p012 → p021 ───────────────────────────────────────────┼→ p020 ──────────────┼→ p024
p007 → p016 ───────────────────────────────────────────┘                     │
          └→ p017 ───────────────────────────────────────────────────────────┘
#15 → p005・p022・p023
```
- p005 は p025 と p004 に依る（p025 が p004 に依るので図は p025 → p005 だけを引く: レビュー 3 Minor 9）。p013 は p012 にも依る（表）。

### 4.2 並べる順の案（P1・P2）

| 段 | P1 | P2 | 理由 |
| --- | --- | --- | --- |
| 1（板の前） | p017（virt、q1274）| p021 → p025（q1278）、p011 (1) の loader の変更の code（板の前に書ける部分） | どれも QEMU・host・build だけ。p016・p004 は cleared。p021 は p012 だけに依る |
| 2 | p013 の code（p025 の merge の後） | p005 の実機、p011 (1) | p013 は p025 の上 |
| 3 | p022・p023（実機）→ p020 → p024 | p014 → p015、p011 (2)（#20・#29 の後） | p014 は PIO なので p023 を待たない |
| 4 | p018 | p019 | |
| 最後 | p006（規約、Q1） | | |

- **P1・P2 が共有する file**: `src/drivers/dma/dw-axi-dmac.c`（p023 の mem→mem の最小と p019 の cyclic: レビュー 3 Minor 12）、`src/kern/platform/visionfive2.c`（init の表・root の選び方・`VF2_DMA_DEFAULT_COHERENT`）、`platform/riscv64/vmunix.mk`（vf2 の object の一覧）、config と menuconfig の項目、`vf2-crg.c` の clock の表（各 Phase が使う clock を足す）、`src/hal/riscv64/`（p011 の console の共通化と p005 の bsp-visionfive2）。各 Phase は自分の行だけを足す。同時に 2 つの Phase が同じ表を変える段 3・4 は、Q1 が merge を 1 つずつ入れて衝突を解く。
- 実機の試験は板が 1 枚なので T1/T2 が順に流す。

## 5. 実機の判定（#15）と接続（#10）

### #15 実機の判定

| 案 | 内容 | 使える時期 | 利点・欠点 |
| --- | --- | --- | --- |
| (A) serial の対話 | `plan/tools/guest/serial.py` に実機の tty を足し、`login:` を待って command を打ち、その command の**答えの文字列**で判定する。AGENTS.md の「serial log を読んで判定しない（解析・回帰・受け入れのどれでも）」に対する、**serial の出力の答えの文字列で判定する例外**（WS256 の実機に限る。レビュー 3 Minor 13） | p005 から | 画面が無くても判定できる。AGENTS.md の規則の例外なのでユーザーの明示の許可が要る |
| (D) JTAG | OpenOCD＋gdb で板の JTAG に繋ぎ、kernel が memory に書いた印（試験の答えの構造体）を gdb で読む。決定 9（sifive_u は gdbstub で読む）と同じ系統 | 機器（JTAG の adapter、VF2 の JTAG の pin）を用意した後 | serial の出力に依らない。機器と配線が要り、OpenOCD の JH7110 の対応は未確認 |
| (B) HDMI の capture | host に UVC の HDMI capture を繋ぎ、boot-test.sh と同じく画面を撮る | p011 で画が出た後 | zedBSD の「画面で判定」に一番近い。機器はユーザーの用意 |
| (C) SSH | p018 の後に `guest.sh` | p018 の後 | 速い。最初の方法にはなれない（§4.1） |

既定: p005〜は (A)、画が出たら (B)（機器があれば）、p018 の後は (C) を足す。(D) は機器があれば (A) の代わりにできる。virt は今までどおり画面。

### #10 実機の接続

| 項目 | 選択肢 | 既定 |
| --- | --- | --- |
| console | (a) 3.3 V の USB-UART を 40 pin の 6・8・10 に、115200、(b) なし | (a)、udev の by-id で名前を固定 |
| 電源 | (a) SwitchBot の plug（`switchbot.py cycle`）、(b) 手で | (a) |
| 媒体の更新（Minor 5 で直した） | (a) SD を host で書いて差し替え、(b) SD の mux、(c) U-Boot の TFTP で file を取り ESP に書く（`tftp ${loadaddr} vmunix; fatwrite mmc 1:3 ${loadaddr} vmunix ${filesize}`。loader は自分を起動した FAT から `vmunix`・`root.img` を読むので、EFI だけを TFTP で取っても kernel は古いまま。`fatwrite`（FAT_WRITE）が Debian の U-Boot にあるかは未確認）、(d) NVMe | **開発も受け入れも (a)**。p005 で `fatwrite` を確かめられたら開発に (c) を足す。p024 の後は (d) |
| LAN | 試験の network に、DHCP の address を固定 | p018 と (c) に要る |
| HDMI | monitor か capture | p011 の時 |

## 6. ユーザーに聞く判断（Q1 経由）

### 6.1 決定済み（問いにしない）

- **#2 板の版と memory**: 決定（2026-10-18 17:46、クリック）「板の版・memory は『つないだ時に記録』」（master.md:691）。p005 で記録する。「gmac0 だけ」は技術の既定として §3.7 に置いた（つないだ時の版で見直す）。
- 手順の出典（2026-10-19 13:39）「手順の参考を許す」（§0.2）。設計・レビューは Opus 5.5（2026-10-19 13:16）。

### 6.2 聞く判断（選択肢・既定・理由）

| # | 問い | 選択肢 | 既定 | 理由・影響 |
| --- | --- | --- | --- | --- |
| 15 | 実機の判定の方法（**p005 の前に要る**） | (A) serial の対話 / (B) HDMI の capture / (C) SSH / (D) JTAG（OpenOCD＋gdb で memory の印を読む）、組み合わせ | (A)→(B)→(C)（§5） | 画面が出るのは p011 の後、SSH は p018 の後で最初にはなれない。(A) は AGENTS.md の「serial log を読んで判定しない（解析・回帰・受け入れのどれでも）」に対する **serial の出力の答えの文字列で判定する例外**（WS256 の実機に限る）なので許可が要る。(D) は決定 9 と同じ系統で serial に依らないが機器が要る。(A) も (D) も却下なら p022・p023・p014 は p011 の画を待つ |
| 10 | 実機の接続 | §5 の表 | §5 の既定（媒体は SD の差し替え） | TFTP は ESP への書き込みが要り、`fatwrite` が未確認 |
| 20 | **配布の U-Boot で GOP が出て ExitBootServices の後も画が残った時に、主の道をどちらにするか**（p011 (1) の後に聞く。それまでは mainline の U-Boot を主にし、配布は SD での試しだけ、flash は書き換えない） | (a) 配布の U-Boot（2021.10）を主にし、mainline の DTB を ESP から `bootefi <img> <fdt>` で渡す / (b) mainline の U-Boot（2025.01）を主のまま、画は自前の display の driver（p011 (b)、未見積）まで無し | (a)。ただし p011 (1) で、loader が boot hart を得られた事（protocol か、(1) で足す `/chosen/boot-hartid` の代わりの道。console の `RV64 HART` の行）と、渡した DTB への `/memory`・reserved-memory の fixup が確かめられた時だけ。どれかが欠ければ (b) | ユーザーの「まずフレームバッファ」（2026-10-19 12:56・13:00）と p015 の「画面で login」に届くのは (a) だけ。(a) の欠点: 古い U-Boot（EFI の実装・fixup が古い）、mainline の DTB を ESP に置く手間。(b) は配布の U-Boot に依らないが、画は大きな別の設計の後 |
| 21 | DTB の出どころ（**決定 2 の解釈**） | (a) U-Boot の control DT / (b) U-Boot が ESP の mainline の DTB を読んで `bootefi` で渡す / (c) loader が差し替える | #20 が (b) なら (a)、#20 が (a) なら (b)。(c) は取り下げ | (b) も「U-Boot が DTB を渡す」で決定 2 の範囲と読むが、ユーザーの確認が要る。(c) は U-Boot の fixup（boot-hartid・memory・reserved-memory・MAC）を全部消す |
| 22 | PMIC（AXP15060・I2C）を外すか | 外す / p013 に入れる | 外す | 要るのは eMMC と CPU の電圧だけ。OpenSBI の I2C5 との競合も避ける |
| 23 | sound の道 | 3.5 mm の PWMDAC / HDMI の audio | PWMDAC | HDMI の audio は自前の display（p011 (b)）が要る |
| 24 | sound の判定 | ユーザーの耳 / host の line-in の録音 | 耳（試験の一覧に載せる） | 機器が要らない |
| 25 | coherency の推定の採否と予備 | 推定（coherent）で設計し p023 で確かめる。非 coherent なら予備 (i): 確保ごとに system port の別名を device の窓に 4 KiB の粒度で写し FLUSH64（hal.h・design.md §2.5 は不変。別名が同じ DRAM である事を p023 の 0a で先に示す）。`constraints.coherent` は汎用の `drv_fdt_dma_coherent` が DT の印（親を辿る）と platform が渡す板の既定で決める | そのとおり | Linux の riscv の既定と、Linux・OpenBSD とも JH7110 で cache 操作をしない事（§1.2）。改訂 2 の「8 GiB を 1 GiB の leaf で写す」は §2.5 を破るので取り下げ、design.md の改訂は要らなくなった |
| 26 | Phase の分け方 | p016（virt）・p020（PLDA）・p021（core の hook）、p013（code）・p022（実機）、**p005 から p025（skeleton）、p017（virt）から p024（実機）** | そのとおり | 板の前に進められ、core の変更の回帰を分けられる。依存を scoped output でなく Phase で名指す |
| 27 | p015 の受け入れを UART の対話で可にするか | 可（画面は p011 の後に追う）/ 不可（p011 を待つ） | 可 | GOP が出ないか #20 が (b) だと p015 が止まる |
| 28 | tools の変更の担当と許可 | serial.py の実機の tty（#15 (A)）と QMP の `send-key`（p016）を、(a) その Phase の担当が変える許可 / (b) Q1 が変える | (b) Q1（tools は共有） | #18 の許可は p009 の 3 file だけ |
| 29 | **HAL の承認**: VF2 の framebuffer（承認の文面は下の 6.3） | (i) 両方を承認 / (ii) 1 だけ承認（console は kernel 側に新規。LW +6〜10、HAL_FATAL は画面に出ない）/ (iii) 却下（p011 は画無し、#27 を可にする） | (i) | §3.1 の土台の選択。hal.h は不変。rpi4（WS044 p006）・virt と同じ形 |
| 30 | p021 の `drv_pci_bus_ops` の hook（hal.h 不変、PCI の内部 API） | 進める / 別の形 | 進める（HAL ではないので承認は要らない見込み。amd64・Pi 4 と共有の pci.c を変えるので知らせる） | §3.5 p021。hook の無い host の道は `pci_intx_dispatch` の分け方だけで変わらない（回帰は T1） |
| 31 | （問いではなく報告: レビュー 3 Minor 9）pci-nvme の INTx の道（amd64 の NVMe と共有の file） | — | INTx の許可を足した（ISR の mask の往復はしない） | §3.6。main に入済み。p017 は T1-723 の FAIL の後 q1274 で P1 が確かめる |

### 6.3 #29 の承認の文面（案）

> riscv64（WS256、VisionFive 2）の HAL の責務の追加。hal.h は変えない。差分は [proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)。
> 1. VF2 の HAL（`src/hal/riscv64/bsp-visionfive2/`）が、loader の handoff 版 2 の framebuffer（UEFI の GOP の今の mode、RGBX・BGRX だけ）を受けた時だけ、`hal_get_arch_handoff` で名前付きの値 `riscv64.framebuffer.phys`・`.size`・`.width`・`.height`・`.pitch`・`.format` を返す。各値は HAL が持つ 64 bit の word への pointer、無い時は NULL。phys は物理 address、size は byte、width・height は pixel、**pitch は byte**、**format は `KERN_BOOT_FRAMEBUFFER_RGBX8888`（1）か `KERN_BOOT_FRAMEBUFFER_BGRX8888`（2）**。virt の HAL は返さない（virt の画面は HAL の ramfb）。
> 2. VF2 の HAL が同じ framebuffer に早期の 80x25 の text console を描く（bsp-virt の console を riscv64 の共通に移して使う。rpi4・virt と同じ責務。描いた後の cache の書き出しは ccache の FLUSH64）。
> 案から変わる時は改めて差分を示す。

## 7. 未確認と Future Work

- 未確認（改訂 4 でレビュー 3 の「確かめていない事」を足した）: system port の別名が同じ DRAM を指すか（p023 の 0a で確かめる）、ccache の FLUSH64 が L1 の dirty な line も書き出すか（inclusive と推測）、`ISTATUS_LOCAL` が mask の外の bit を示すか、配布の U-Boot 2021.10 に `RISCV_EFI_BOOT_PROTOCOL` が backport されているか（p011 (1) の `RV64 HART` の行で分かる）、U-Boot の efi_gop が framebuffer を EFI の map のどの型で出すか、VF2 の実際の `/reserved-memory` と EFI の map の数、driver が使える 1 秒の待ちの timer の API（p021 の drain の上限）、OpenOCD の JH7110 の対応（#15 (D)）。VL805 の型番と USB・M.2 がどちらの PCIe か。JH7110 の DMA と DC8200 の coherency。GOP が出るか、ExitBootServices の後に画が残るか、U-Boot が GOP の範囲を EFI の map・DT でどう示すか。U-Boot の `bootefi <img> <fdt>` が渡された DTB に fixup を当てるか。配布の U-Boot の `RISCV_EFI_BOOT_PROTOCOL`。板の版・memory（つないだ時に記録）。PHY の id と v1.2A の PHY。PWMDAC の register。`ISTATUS_LOCAL` が latch か level か、PLDA の INTx の bit が handler の後に立ち直るか。PLDA の inbound の窓の reset の値。OpenSBI の電源断の実装と I2C5。DW MMC の HCON の DMA64。system port の別名に PMP が掛かるか、別名での LR/SC・AMO。Debian の u-boot-starfive の `CMD_CLK`・`FAT_WRITE`。VL805 の DMA の bit 数。ExitBootServices で U-Boot が PCIe の device を止めるか。
- Future Work（Q1 が future-work.md に足すかを決める）: SD の protocol の rpi4-sdhci.c との共有化、PLDA の MSI の demux、DW MMC の IDMAC を p014 で入れなかった時、eMMC と PMIC。

## 8. Q1 への依頼

- **HAL の承認**: #29（§6.3 の文面、[proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)）。
- **design.md の直し**（この文書からは直さない）:
  1. §1.6 の「JH7110 の DMA は非 coherent」→ §1.2 の推定と p023 に。§1.6 の「`hal_pmem_map_uncached` は VF2 では UNSUPPORTED」に「p023 で非 coherent なら予備 (i)（§2.4）」を足す。
  2. §2.5 の表の「予約 30 GiB」→ entry 448〜509 は **62 GiB**（Minor 13）。予備 (i) は §2.5 を変えないので、改訂 2 で頼んだ「448〜509 に写す」の確認は取り下げる。
  3. §3.2 の起動の switch（`MSEL[1:0]: 10 SD、00 flash`）を RGPIO_1/0 の 01 SD・00 flash に。
  4. §3.2 の DTB の扱いに §2.2（`bootefi <img> <fdt>`）と #20・#21 を足す。
  5. §1.6・§5.1 の「VF2 の実機には画面が無い」「VF2 の BSP に framebuffer は無い」を、p011 (2) の HAL の console（#29 の後）に。§2.2 の file の一覧に console の共通化（`cons.c`・`font.c`・`fbcons.c`）。
  6. §3.4 (b)「memory は handoff の ZBL6 v6 の範囲（UEFI の map が正）」を、p004 の実装（DT の `/memory` から作り、版 2 で loader が firmware の使う範囲を `reserved[]` で渡す、§3.1 (a)）に合わせる。
  7. §6 の p005 の行を p025（skeleton）と p005（実機）に分ける。
- **ws.md**: p020〜p025 の行を planning で足し、p016・p017 の中身を設計に合わせた（状態の欄は P1 の q1271 の物で変えていない）。p005・p011・p013〜p015・p018・p019 の依存と LW も §4.1 に合わせた（この commit）。Master の WS の一覧の写しに変わりは無い。
- **tools**（#28）: serial.py の実機の tty、QMP の `send-key`。
- **範囲外の観察（bug にするかは Q1）**: riscv64 と arm64 の `hal_irq_unregister` は実行中の handler を待たず、core の INTx・MSI の解除の comment（pci.c:3560-3585「HAL removal is a checked drain barrier」）と合わない。p021 の hook の道は host が自分で drain するので影響しないが、hook の無い道（virt の ECAM・Pi 4）の解除には残る。
- `build/tmp/ws256-docs/starfive,jh7110-crg.h` の片付け。

## 9. レビュー 1 への対応

| 指摘 | 対応 | 所 |
| --- | --- | --- |
| B1 PLDA の INTx を core が配れない | 直した: `drv_pci_bus_ops` に省略可能な INTx の establish・disestablish・mask/unmask の hook（hal.h 不変）、host が線を 1 回登録し `ISTATUS_LOCAL` で demux、handler の後に W1C の ack。p021 に分け、amd64・arm64・riscv64 の build と Pi 4 の xHCI・amd64 の xHCI/NVMe の回帰を T1 に。LW 3〜5（改訂 3 で作り直した: §10 BL1・MJ1・MJ2） | §3.5 |
| B2 pci-nvme が MSI 必須 | 直した: pci-nvme に INTx の道を足す案を選んだ（理由 3 つ）。PLDA は INTx だけと明記、MSI は Future Work。amd64 の NVMe の回帰を計画 | §3.6、§7 |
| M1 framebuffer の HAL の責務と表示の cache | 直した: `riscv64.framebuffer.*` の差分の案を proposed/ に置いた（適用しない）。coherency の二案 (α)(β) と 3 通りの試験（改訂 3 で土台と文面を直した: §10 MJ3・MJ4） | §3.1、proposed/hal-riscv64-framebuffer.md |
| M2 coherency の手順と時期 | 直した: p023 に統一し、dirty の line・stale の clean line・両方向・bus master ごと・scratch の領域の手順（改訂 3 で大きさと陽性の対照を直した: §10 MJ6） | §1.2、§2.4、§0.1 |
| M3 予備の案 | 直した: (ii) を取り下げ (i) だけ。uncached の根拠を Table 2-5 の `RW A` に（改訂 3 で (i) の形を直した: §10 MJ7） | §1.2、§2.4 |
| M4 NVMe の root の過小 | 直した: pcat の形を visionfive2.c の新規に（改訂 3 で p024 に移した） | §3.11 |
| M5 U-Boot と DTB | 直した: mainline の U-Boot を主、loader の差し替えを取り下げ、`bootefi <img> <fdt>`（改訂 3 で分岐を直した: §10 MJ5） | §2.2、§6 #20・#21 |
| M6 依存と並行の表 | 直した: p013 を code と p022（実機）に分けた。p016 は p004 不要で p007 の後。共有の file を全部挙げた（改訂 3 で p025・p024 を分けた: §10 Minor 6） | §3.2、§4 |
| M7 GPL の手順の出典 | 決着として書いた: ユーザーの決定（2026-10-19 13:39）（改訂 3 で決定の外の PLDA の MSI を外した: §10 Minor 3） | §0.2 |
| M8 sd-card の切り出し | 直した: 既定を「切り出さない」に、DW MMC は PIO から | §3.3、§7 |
| Minor 1〜6・8〜11・13〜17 | 直した（改訂 2 の §9 の表のとおり。git の履歴の改訂 2 を参照） | — |
| Minor 7 CRG の id の register | 取り下げ、U-Boot の `clk dump` との照合に（改訂 3 で `md` に直した: §10 Minor 8） | §3.2 |
| Minor 12 ID の形 | p016・p020〜p023（改訂 3 で「仮」を外した: §10 Minor 4） | 冒頭、§4.1 |

## 10. レビュー 2 への対応

| 指摘 | 対応 | 所 |
| --- | --- | --- |
| **BL1** hook に渡す `dispatch` が EOI を送り riscv64 で HAL_FATAL | 直した: core の `pci_intx_dispatch` の本体を **EOI を送らない `intx_line_run(void *line)`** に分け、hook には ack を持たない型 `drv_pci_intx_run_t` で渡す。EOI は host の PLIC の handler が W1C の後に **1 回だけ**送る（status 0・event だけの時も）。hook の無い host は `run` → EOI で今と同じ。host の試験に「2 本が同時」「event だけ」「status 0」 | §3.5 p021 の 1・6、試験 1・2・6、p020 の試験 |
| **MJ1** 線を vector の番号だけで引き 2 本の PLDA で衝突 | 直した: 線の鍵を **(host, vector)** にし、`pci_intx_line` に `ops`・`host` を持たせる（hook の無い host は NULL で今の全体の番号）。vector は host の pin（intc の hwirq 1〜4）なので、解除と mask の時に host は pin を知る。試験に「2 つの host・同じ hwirq」 | §3.5 p021 の 2、試験 3 |
| **MJ2** lock の順と drain・戻り値が無い | 直した: establish・mask・unmask は `intx_lock` の中の spin の文脈（眠らない・確保しない、順は `intx_lock` → host の lock だけ）、host は lock を外してから `run` を呼び pin ごとの `in_flight` で数える、`intx_disestablish` は thread の文脈で `in_flight` が 0 になるまで待ち（上限 1 秒）、超えたら元に戻して EBUSY。戻り値 0・EBUSY・EINVAL と core の EIO への写し方 | §3.5 p021 の 4・5、試験 4・5 |
| **MJ3** kernel 側の console が無い、handoff に framebuffer も予約も無い、LW が過小 | 直した: **描画は HAL に置く**（bsp-virt の console を riscv64 の共通に移し VF2 でも使う。kernel の新しい console は作らない、理由 3 つ）。riscv64 の handoff を**版 2** にして framebuffer の field と `reserved[]`（16 個、framebuffer の範囲を必ず含む、超えたら loader が止まる）を足し、loader が GOP と EFI の map から入れる。HAL が `reserved[]` を allocator から、(α) なら direct map からも外す（上限超えは HAL_FATAL）。依存は「p004 の後に p011 が handoff を広げる」。LW を (1) 1〜2、(2) 9〜12（計 10〜14）に付け直した | §3.1 (2) (a)〜(c)、§1.5、§4.1 |
| **MJ4** HAL の差分の案の誤り | 直した: hal.h:1284-1292 の形（1 引数、無い時 NULL、値は 64 bit の word への pointer）、pitch は byte、format は `KERN_BOOT_FRAMEBUFFER_*`、返すのは VF2 の BSP だけ（virt は返さない）。`HAL_ERR_NOT_FOUND` を消した。#29 の文面を §6.3 に | proposed/hal-riscv64-framebuffer.md、§6.3 |
| **MJ5** U-Boot の分岐の論理が逆 | 直した: 「(1) で出ない時は配布を主に」を消し、**#20 を「配布の U-Boot で GOP が出た時に主をどちらにするか」**に（(a) 配布を主＋mainline の DTB を `bootefi` / (b) mainline のまま画は自前の driver まで無し。既定 (a)、boot-hartid と fixup が確かめられた時だけ）。出ない時は (b) だけ | §3.1 (1)、§6.2 #20・#21 |
| **MJ6** coherency の確かめが偽の合格を出す | 直した: buffer を各 **64 KiB**（L2 2 MiB・16 way の 1 way の半分。各 set に 1 line）、src を書く → dst を読む → 直ちに転送、offset を変えて 8 回。**陽性の対照**（system port の別名から dst に違う模様を書いて stale を検出できる事）を先に流し、検出できなければ判定しない。判定は cacheable の view、別名は分類だけ | §2.4 |
| **MJ7** 予備 (i) が design の決定を変え危ない写像を作る | 直した: **確保ごとに** `paddr + 0x4_0000_0000` を device の窓に写す（FLUSH64 の後）。allocator の RAM の別名だけを写し、OpenSBI・予約の別名に触れない。design.md §2.5・§2.1 は不変で、#25 から design の改訂を外した。`constraints.coherent` は `vf2_dma_coherent()`（DT の `dma-noncoherent`・`dma-coherent`、無ければ板の既定）を全部の DMA の glue と PLDA の root の DMA の device が呼ぶ。0 で (i) が無ければ ENOTSUP で見える失敗 | §2.4、§3.2、§6.2 #25 |
| Minor 1 pci-nvme の INTx の処理 | 直した: INTx の許可だけを足す。5457-5458 の INTMC が既に INTx を覆う。ISR で INTMS/INTMC の往復をしない（disable との競合）。要る時は `command_lock` の中で `controller_enabled` を見る | §3.6 |
| Minor 2 #2 は決定済み | 直した: §6.1 に決定として書いた（master.md:682）。「gmac0 だけ」は技術の既定として §3.7 | §6.1、§3.7 |
| Minor 3 PLDA の MSI の GPL の参考 | 直した: §0.2 の表から外し、決定 13:39 の一覧の外に広げない事を明記 | §0.2 |
| Minor 4 Phase の番号と記録の食い違い | 直した: p020〜p025 は ws.md の次の番号で「仮」を外した。ws.md の p012 の行の「改訂 3」は今回の改訂で本文と合う | 冒頭、§4.1、ws.md |
| Minor 5 #10 の TFTP の既定 | 直した: 既定を開発も受け入れも (a) SD の差し替えに。(c) は `tftp` の後に `fatwrite` で ESP に書く手順を書き、`fatwrite` が確かめられたら足す | §5 #10 |
| Minor 6 p005 の skeleton と p017 の virt/実機 | 直した: **p025（VF2 の skeleton、p005 から分けた）**と **p024（NVMe の実機と root、p017 から分けた）**。p013 の依存は p025 | §3.10・§3.11、§4.1、ws.md |
| Minor 7 判定と依存の循環、試験の command の形 | 直した: #15 を p005・p022・p023 の前提として依存の表に入れ、(A) 却下の時の帰結と (C) が最初になれない事を書いた。試験の形は試験の kernel の config が作る cdev（`/dev/vf2-dmatest`、`/dev/vf2-clktest`）を `cat` して答えを読む | §2.4、§4.1、§6.2 #15 |
| Minor 8 `clk dump` | 直した: U-Boot の `md.l` で PLL と CRG の register を読み、host の試験の計算と kernel の答えと DT の `assigned-clock-rates` で照合 | §3.2 p022 |
| Minor 9 bridge をまたぐ swizzle | 直した: p020 の `allocate_irqs` に pci-brcmstb.c:1505-1578 と同じ形の swizzle | §3.5 p020 |
| Minor 10 (β) の FLUSH64 の費用 | 直した: grid（文字の shadow）から描き、fence は描画ごとに前後 1 回、scroll は 80x25 の領域で 16,000 回、全面は初めの塗り 1 回だけ | §3.1 (d) |
| Minor 11 DMA の bit 数と inbound の窓 | 直した: DT に `dma-ranges` が無く OpenBSD も inbound を書かない事を事実に足し、p020 で inbound の ATR を読む。既定は root の DMA の device を 32 bit、DDR 全体を 1:1 で覆うと確かめたら 64 bit | §1.1、§3.5 p020 |
| Minor 12 U-Boot が動かした PLDA・VL805・NVMe | 直した: mainline の PREBOOT（`nvme scan; usb start`）と `PCI_INIT_R` を事実に（改訂 2 は `nvme scan` を書き落とした）。p020 の最初に HCHalted・CC.EN を読み、PERST で落として学習し直す。p024 の試験にも | §1.4、§3.5 p020、§3.11 |
| Minor 13 design.md §2.5 の 30 GiB | Q1 への依頼にした（62 GiB）。予備 (i) は §2.5 を使わない | §8 の 2 |
| 範囲外の観察（`hal_irq_unregister` が drain しない） | Q1 に bug にするかを頼んだ。p021 の hook の道は host が自分で drain するので依らない | §1.5、§8 |
| 直さなかった物 | 無し（全部を当てた）。実機・U-Boot に依る点（`ISTATUS_LOCAL` の latch、bootefi の fixup、DC8200 の snoop、DMA の coherency、別名の PMP・atomic、GOP の memory の型）は未確認のまま §7 | §7 |

## 11. レビュー 3 への対応

[レビュー 3](design-peripherals-review-3.md)（Blocker 0・Major 2・Minor 13）。確認者の結論は「Major 1・2 を直す小さな改訂の後なら受け入れてよい」「p021・p025・p013 は今の設計で着手してよい（Minor 3・4・5・8 を当てる）」。

| 指摘 | 対応 | 所 |
| --- | --- | --- |
| **Major 1** p011 (1) と #20 (a) が今の loader（protocol だけ、GOP の code 無し）では動かない | 直した: p011 (1) に loader の小さな変更を code として足した。protocol が無い時は EFI の DT の `/chosen/boot-hartid` を読み（DT の複写を `find_boot_hart` の前に移し、`/chosen` の 1 つの property の最小の読み手）、どちらを使ったかを console に出す。GOP の今の mode を読むだけで答えに出す（`SetMode` しない、handoff には入れない）。host の試験（5 つの DT の blob）・build・riscv64-uboot の回帰。LW (1) 1〜2 → 2〜4、p011 は 11〜16、合計 69〜108。#20 (a) の条件を「loader が boot hart を得られた事」に | §1.4、§3.1 (1)・依存、§4.1、§6.2 #20、ws.md |
| **Major 2** 別名が同じ DRAM である事が未確認、予備 (i) が 2 MiB の粒度で OpenSBI の別名も写す | 直した: 陽性の対照を 2 段に（0a 別名が同じ DRAM である事: cacheable で書く → FLUSH64 → 別名で読む、別名で書く → FLUSH64 → cacheable で読む。0b stale の検出）。INCONCLUSIVE・MISMATCH の時は主の試験だけで判定し、別名は分類にも使わない。MISMATCH なら (i)・(α) は採れず Q1 に返す。試験の別名は試験が allocator から取った 2 MiB 揃えの 2 MiB だけを写す。予備 (i) は **device の窓の別名の範囲に 4 KiB の段を足す**（space.c の内部、確保した page の別名だけを有効に、外さない）を選び、専用の pool は採らない理由を書いた | §2.4 の 0・3・5、予備 (i) の 2〜4、§6.2 #25、§7 |
| Minor 1 版 2 の読み分けが cmain.c:159-162 で壊れる、`reserved_flags` の意味が無い | 直した: 判定を `version < 1` と版 1 の終わり（`offsetof(parameters) + sizeof`）に、版 2 は `version ≥ 2 && size ≥ 版 2 の終わり`。`_Static_assert`（版 1 の終わり 3160、8 の倍数、1 page 以下）。`reserved_flags` の bit 0 は `ZBL6_RISCV64_RESERVED_OVERFLOW` | §3.1 (2) (a)、proposed/hal-riscv64-framebuffer.md |
| Minor 2 `reserved[]` の上限を DT と共有、超えた時の判定の時期 | 直した: loader の範囲は別の配列 `firmware_reserved[16]`、DT の予約に含まれる物は飛ばして合わせる。最初の `GetMemoryMap` で数えて超えたら画面に出して止め、再試行の中で超えたら OVERFLOW の bit を立てて HAL が HAL_FATAL | §3.1 (2) (a)・(b) |
| Minor 3 host の lock の文脈 | 直した: host の lock は全部の所で irq-save（同じ hart の ISR が待ち続けないように） | §3.5 p021 の 4 |
| Minor 4 試験 4 の期待が実装と合わない、runner の rm、既存の試験 | 直した: fake の host の ISR を `in_flight++` と `run` の間で止めて drain を試す（待って 0、上限で EBUSY、handler の中からは core の EBUSY）。runner は mktemp と rm の trap を写さず `plan/tools/fresh-out.sh`。既存の `plan/ws004/tests/pci-shared-intx-test.c` 等が壊れた時は AGENTS.md の整理の基準で直すか削除を Q1 に頼む。§3.5 の 5 の例も直した | §3.5 p021 の 5・試験 |
| Minor 5 `vf2_dma_coherent()` の層と意味 | 直した: 汎用の `drv_fdt_dma_coherent(fdt, node, board_default)`（親を辿る）を `src/drivers/generic/fdt.c` に、板の既定 `VF2_DMA_DEFAULT_COHERENT` は platform（visionfive2.c）が attach の引数で渡す。`pci-ecam-fdt.c` の逆の意味は virt の既定として残す | §2.4、§3.2・§3.3・§3.5・§3.7、§6.2 #25 |
| Minor 6 (α) の写像の時期 | 直した: (α) の別名は root の切り替えの前に `map_hal_devices` で写す | §3.1 (2) (d) |
| Minor 7 (α) の nomap と `hal_space_map_device` | 直した: (α) を選ぶ時に `riscv64_range_is_ram`・`_touches_ram` から nomap を除く（src/hal の実装の変更） | §3.1 (2) (d) |
| Minor 8 p025 の skeleton が link しない | 直した: BSP の口に `hal_dcache_clean_range` など 3 つがある事を書き、p025 で ccache の FLUSH64 で実装する（空の stub にしない） | §3.10 |
| Minor 9 記録の食い違い | 直した: ws.md の p006 の範囲を p011〜p025 に、表の見出しと実行の順の文に p020〜p025。§4.1 の図に p025 → p005。§4.2 の段 1、§3.5 p016 の繋ぎ（済み）、§3.6 の行の番号（1743・5557・2980）と p017 の今の状態、#31 を報告に、master.md:691 | ws.md、§3.5、§3.6、§4.1、§4.2、§6.1、§6.2 #31 |
| Minor 10 p021 の回帰に riscv64 virt | 直した: T1 の回帰に virt の usb-kbd（T1-723 の走り B と同じ）を足した | §3.5 p021 の build・回帰 |
| Minor 11 PLDA の ISR が mask した pin を配りうる | 直した: `status & 許可した pin の bit` だけを配る、vector v ↔ bit 24 + (v − 1) | §3.5 p021 の 6 |
| Minor 12 §2.4 の細部 | 直した: 別名の leaf は試験の後も残る事、短い版の手順（DMAC・SDIO・xHCI・NVMe・GMAC の src→dst）、`dw-axi-dmac.c` を共有の file に | §2.4、§4.2 |
| Minor 13 #15 の書き方と (D) JTAG | 直した: (A) を「serial の出力の答えの文字列で判定する例外」と書き、(D) JTAG（OpenOCD＋gdb）を選択肢に足した | §2.4、§5、§6.2 #15、§4.1 |
| 直さなかった物 | 無し。資料で確かめられない事（別名の DRAM・PMP、`ISTATUS_LOCAL`、FLUSH64 の inclusive、2021.10 の protocol、GOP の map の型、`/reserved-memory` の数）は §7 の未確認に | §7 |
