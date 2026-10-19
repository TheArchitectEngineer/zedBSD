# WS256 VisionFive 2 の周辺の設計（p012、改訂 2）

- Phase: [ws256-p012](phase012/phase.md)。版 1 は q1270-i01（2026-10-19）、**改訂 2 は q1270-i02（2026-10-19、P1）**: [レビュー 1](design-peripherals-review-1.md)（Blocker 2・Major 8・Minor 17）の全部を当てた。対応の表は §9。
- 範囲: p011（framebuffer）と p013〜p019（と、この改訂で分けた新しい Phase、§4）の設計。code は書かない。HAL の差分は案だけで適用しない（[proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)）。
- 前提: [design.md](design.md)（改訂 2）の HAL の芯・起動の道・ユーザーの決定 1〜12。design.md の直しが要る所は §8 に Q1 への依頼として集めた（この文書からは直さない）。
- **改訂 2 は design-reviewer にまだ通していない**（P1 は子のエージェントを起動しない。Q1 が回す）。
- **Phase の ID（仮）**: この改訂で新しく要る Phase を p020〜p023 と仮に書く。番号は Master の next で Q1 が決め、決まったら置き換える（§4.1）。

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
| **linux-v6.12-arch-riscv-Kconfig**（改訂 2 で追加） | https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/riscv/Kconfig | f344838853cd008f9252d72a82b19a8e655d73d57431ae60fba71958f6e3c377 | GPL-2.0。20 行 `select ARCH_DMA_DEFAULT_COHERENT`（§1.2） |
| **linux-v6.12-kernel-dma-Kconfig**（同） | https://raw.githubusercontent.com/torvalds/linux/v6.12/kernel/dma/Kconfig | 0bba904da9ea8dec2e32be7deff901ece9b6a5e67495c59a70953fe92890ce8b | GPL-2.0。85 行 `config ARCH_DMA_DEFAULT_COHERENT` |
| **linux-v6.12-drivers-of-address.c**（同） | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/of/address.c | 56e3ebb0b3b2a822f0fbdab5b2488ffc91fafddd345fa595ce8e006dd9e17640 | GPL-2.0。`of_dma_is_coherent`（1001〜1019 行: `dma-coherent`・`dma-noncoherent` が無ければ `dma_default_coherent`） |
| linux-v6.12-drivers-of-device.c（同、読むだけ） | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/of/device.c | cf8f77133cf15587976eeb4adf0d4116f1890fe93770ddc98da7c378d06cb2a8 | GPL-2.0 |
| **obsd-sfcc.c**（同） | https://raw.githubusercontent.com/openbsd/src/master/sys/arch/riscv64/dev/sfcc.c | 637b04841d2bb980273c080ad1da547ebde309b969bdc309fa41d87955f6b2cd | ISC。62〜63 行で `sifive,fu540-c000-ccache` と `starfive,jh7100-ccache` だけに attach（jh7110 は無い） |
| u-boot-vf2-defconfig | https://raw.githubusercontent.com/starfive-tech/u-boot/JH7110_VisionFive2_devel/configs/starfive_visionfive2_defconfig | e051baa92489f53dddd0b5dfd4965fe0e0d0772985850b040b349f5266ed3272 | GPL-2.0+。配布の U-Boot の構成 |
| u-boot-mainline-vf2-defconfig | https://raw.githubusercontent.com/u-boot/u-boot/v2025.01/configs/starfive_visionfive2_defconfig | 25b00e347cc7c40265df192fd59095bda1c3219f35e9d1668e2e8c5eb7880f91 | 同上。mainline（44 行の PREBOOT に `setenv fdt_addr ${fdtcontroladdr}` と `usb start`） |
| README.md（StarFive の SDK） | https://raw.githubusercontent.com/starfive-tech/VisionFive2/JH7110_VisionFive2_devel/README.md | d9ef3d14363c59f4dbfba54a24fc4dbf881b8668c2987569cfbe4dc28a06ea1a | SDK の U-Boot の版（2021.10） |
| obsd-stfclock.c・stfpcie.c・stfpciephy.c・stfpinctrl.c（`sys/arch/riscv64/dev/`）、dwmmc.c・if_dwqe_fdt.c・axppmic.c（`sys/dev/fdt/`）、dwqe.c（`sys/dev/ic/`）、ytphy.c（`sys/dev/mii/`） | https://raw.githubusercontent.com/openbsd/src/master/<path>（master 859883f17cb8e20088fece5398547e4264ad30b3、2026-10-19T03:16:39Z。sfcc.c は同日の master） | `SHA256SUMS` | ISC。手順の参考。code は写さない |

`SHA256SUMS` に全部（改訂 2 の 5 file も追記した）。`starfive,jh7110-crg.h` は reset の header と同一の重複（片付けは Q1）。

### 0.2 参照の扱い（M7 の決着）

- 形は [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」とユーザーの決定 6（「WS245 と同じ」）: 値は block ごとに `Value(s) referenced:`、手順は `Procedure referenced:` を block の直前の複数行の comment に書く。file の先頭には書かない。code・comment・構造・symbol の名前は写さない。Linux・U-Boot の source は読むだけで tree に入れない。
- **ユーザーの決定（2026-10-19、クリック）**: 手順の出典が Linux（GPL）しか無い部品は「手順の参考を許す」（i915 と同じく code・comment・構造は写さず、block ごとに Procedure/Values referenced:）。
- 部品ごとの出典の表（手順の順位は TRM → ISC（OpenBSD）→ GPL の手順の参考）:

| 部品 | register の値 | 手順 | GPL の手順の参考 |
| --- | --- | --- | --- |
| SYS/STG/AON CRG・SYSCON・IOMUX・PMU | TRM | OpenBSD stfclock.c・stfpinctrl.c | 不要 |
| DW MMC（基本） | OpenBSD dwmmc.c の値、Linux dw_mmc の値 | OpenBSD dwmmc.c（PIO・IDMAC とも） | 不要 |
| DW MMC の JH7110 の sample phase（`starfive,sysreg`） | Linux dw_mmc-starfive | **Linux**（OpenBSD の glue に tuning は無い） | **許す（決定）** |
| PLDA PCIe・PHY | OpenBSD stfpcie.c・stfpciephy.c | OpenBSD | 不要（MSI を後で足すなら Linux pcie-plda-host を参考可） |
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
| PCIe | 2 本の PCIe 2.0 x1（pcie0 cfg 0x9_4000_0000・apb 0x2b000000、pcie1 0x9_c000_0000・0x2c000000、DTS の cfg の reg は 16 MiB）。IP は PLDA。PHY pciephy0/1。**INTA〜D・MSI・event は 1 本の PLIC の線（pcie0 56、pcie1 57）にまとまり、host が `ISTATUS_LOCAL` を読んで振り分け W1C で ack、`IMASK_LOCAL` で pin ごとに unmask**（obsd-stfpcie.c `stfpcie_intr`・`stfpcie_intx_intr_establish`）。`interrupt-map` は自分の子の `pcie_intc0/1` を指す。PERST sysgpio 26・28。窓 32 bit 0x30000000/0x38000000（各 128 MiB）、64 bit prefetch 0x9_0000_0000/0x9_8000_0000（各 1 GiB）。**STG SYSCON の offset（0x048・0x1f8 など）は DT に無く、controller の cfg の address で 0/1 を選ぶ**（obsd-stfpcie.c:271-281） | DTS、TRM、OpenBSD | XpressRICH の名前は公開資料に無い。`ISTATUS_LOCAL` が latch か level か（ack が要る前提で設計） |
| USB 3.0 ×4 | PCIe の先の xHCI（U-Boot `USB_XHCI_PCI`） | VF2 datasheet、defconfig | **VL805 の名前は公開資料に無い**。どちらの PCIe かも未確認（p016 系の実機の最初に U-Boot の `pci` で vendor・device を読む） |
| SoC の USB | `usb0`（Cadence、peripheral） | DTS | 範囲外 |
| M.2 | M-Key、PCIe 2.0 x1 | VF2 datasheet | どちらの PCIe か未確認 |
| GbE | gmac0（0x16030000）・gmac1（0x16040000）、`snps,dwmac-5.20`、RGMII。gmac0 は `starfive,syscon = <&aon_syscon 0xc 0x12>`（**AON SYSCON**、jh7110.dtsi:1028）。v1.3B は `starfive,tx-use-rgmii-clk` を持つ → TX の clock の set_rate は不要（obsd-if_dwqe_fdt.c:284-286）。v1.2A の gmac1 は RMII | DTS | **PHY の型番**: VisionFive2_Datasheet に YT8531 の名は無い（grep 0 件）。JH7110 datasheet の対応 PHY の表（YT8521/YT8531）からの推定 → p018 の最初に MDIO で PHY の id を読んで決める |
| PMIC | AXP15060、I2C5（0x12050000）の 0x36。dcdc1 3.3 V always-on、dcdc2 CPU、aldo4 eMMC の vqmmc。PMIC を参照するのは mmc0 と cpu だけ | DTS | OpenSBI が電源断で I2C5 の PMIC を触る可能性（推測） |
| sound | 3.5 mm jack。**PWMDAC**（0x100b0000、`starfive,jh7110-pwmdac`）、`simple-audio-card`、codec は dummy。**PWMDAC の node に `interrupts` は無い**。`dmas = <&dma 22>` の 22 は **DMAC の handshake（request line）の番号**で、DMAC の channel は `dma-channels = <4>`（jh7110.dtsi:557-565・1063-1074） | DTS | PWMDAC の register は TRM に無い（Linux の値、§0.2） |
| HDMI | TRM の map に DC8200・`U0_HDMITX`（0x29590000）・VOUT_CRG。mainline v6.12 の DTS には display の node が無い | TRM、DTS | p011 |
| 40 pin の UART | pin 8 TX（GPIO5）、10 RX（GPIO6）、6・9 GND | VF2 datasheet §5.1 | — |
| boot の switch | RGPIO_1/0: 00 QSPI flash、01 SD、10 eMMC、11 UART（TRM Table 2-2 も同じ: 0x1 SDIO、0x2 eMMC）。**design.md §3.2 の「MSEL[1:0]: 10 SD」は誤り → §8 で Q1 に直しを頼む** | VF2 datasheet、TRM | 板の今の設定 |
| 再起動 | `gpio-restart`（sysgpio 35） | DTS | 電源断は OpenSBI の SRST（未確認） |

### 1.2 DMA の coherency（design.md §1.6 を直す。M2 の根拠を足した）

- 事実（2026-10-19）:
  1. Linux v6.12 の `sifive_ccache.c` の非 coherent の cache 操作（FLUSH64）は `starfive,jh7100-ccache` だけに登録（123-124・318-322 行）。
  2. OpenBSD の `sfcc.c` も fu540 と jh7100 だけに attach する（62-63 行）。
  3. **Linux の riscv は `ARCH_DMA_DEFAULT_COHERENT` を select する**（arch/riscv/Kconfig:20）。`of_dma_is_coherent` は `dma-coherent`・`dma-noncoherent` が無い時 `dma_default_coherent` を返す（drivers/of/address.c:1001-1019）。→ riscv では「DTS に `dma-noncoherent` が無い」=「Linux は coherent と扱う」の意味を持つ。
  4. `jh7110.dtsi` に `dma-noncoherent` は 0 件。
  5. TRM Table 2-4 は DDR を 0x0_4000_0000〜（memory port、`RWX C A N`）と 0x4_4000_0000〜（system port、`RWX N`）の 2 窓で書く。**uncached の根拠は Table 2-5（U74 の map）の `0x04_4000_0000 RW A`（C 無し）**（TRM の text の 2207 行）。属性 N は memory port にも付くので uncached の意味ではない（版 1 の誤り、M3）。
- → **JH7110 の周辺の DMA は coherent と推定**（Linux・OpenBSD とも JH7110 で cache 操作をしないまま動いている）。design.md §1.6 は JH7100 の取り違えと見る（§8 で直しを頼む）。**推定のまま。実機の確かめは §2.4。**
- **表示（DC8200）の coherency は別に扱う**（scan-out は CPU の書き込みを読むだけで、snoop が無ければ L2 に残った画が出ない）。§3.1。

### 1.3 割り込みの番号

- TRM の `global_interrupts[n]` と DTS は n + 5（UART0 27→32、PLDA 51→56、SDIO1 70→75）。driver は DTS の値だけを使う。

### 1.4 U-Boot と framebuffer

- 配布の U-Boot（2021.10）: `DM_VIDEO`・`VIDEO_STARFIVE`・`DISPLAY_STARFIVE_HDMI`・`VIDEO_COPY`、`drivers/video/starfive/`。PREBOOT は `run chipa_set_uboot;`（`usb start` は無い: Minor 8）。
- mainline の U-Boot v2025.01: VIDEO 無し、PREBOOT に `setenv fdt_addr ${fdtcontroladdr}`・`usb start`。OF_UPSTREAM で mainline の DTS から作った control DT を渡す。
- → **GOP が出る見込みは配布の U-Boot だけ**（U-Boot の EFI は video の device があれば GOP を出す。VF2 で出すかは未確認）。ExitBootServices の後に画が残るか（video の device の remove で display を止める場合がある）も未確認。
- 配布の U-Boot は `RISCV_EFI_BOOT_PROTOCOL` を持たない可能性がある（追加は 2022 年、design.md:288）。その時 boot hart は `/chosen/boot-hartid` に頼る。

## 2. 共通の設計

### 2.1 driver の置き場所と境界

- **HAL**: design.md のとおり ccache・CPU・timer・PLIC・SBI の console だけ。周辺の clock・reset・pinctrl・GPIO・I2C は HAL に入れない。
- **driver**: `src/drivers/platform/visionfive2/`（板・SoC に固有）と、IP が汎用の物は `src/drivers/<種類>/`（`mmc/dw-mmc.c`、`ethernet/dw-eqos.c`、`ethernet/yt8531.c`、`pci/pci-plda.c`、`pci/pci-ecam-fdt.c`、`dma/dw-axi-dmac.c`）。DW I2C の FDT 版は PMIC を外すので作らない（#22）。
- **kernel の platform**: `src/kern/platform/visionfive2.c`（p005 が skeleton を作る）が device の init の順と root の選び方を持つ（rpi4.c の形）。

### 2.2 device tree と U-Boot（M5 で改訂）

- **ユーザーの決定 2（DTB は U-Boot から）を保つ**。既定は **mainline の U-Boot（Debian の `u-boot-starfive` 2025.01 を SD に置く、design.md §3.2 の既定）の control DT**。これは mainline の DTS から作る物で、kernel が支える binding（v6.12）と同じ。U-Boot の fixup（`/chosen/boot-hartid`、`/memory`、OpenSBI の reserved-memory の複写、`local-mac-address`）がそのまま付く。
- **loader で DTB を差し替えない**（fixup が全部消える。版 1 の #21 の既定を取り下げた）。別の DTB が要る時（例 配布の U-Boot を試す p011 (1)）は、U-Boot の `load mmc … ${fdt_addr_r} dtb/…` と `bootefi <img> ${fdt_addr_r}` で渡す。U-Boot は bootefi に渡された DTB にも fixup を当てる見込み（**推測**、p005 で `/chosen` と reserved-memory を読んで確かめる）。
- **配布の U-Boot（2021.10）は p011 (1) の試しに限る**（GOP が出るかを見るため）。主の道にしない。
- clock と reset の番号は binding の header の値を Value(s) referenced で使う。

### 2.3 試験の道

- QEMU に JH7110 の machine は無い。JH7110 の固有の block は実機だけ。
- QEMU の virt で試せる物: PCIe の先の既存の driver（xHCI・usb-hid・NVMe）の riscv64 での動作（p016、ECAM の FDT host、INTx だけ）。virt の DMA は coherent。
- host の試験（`plan/ws256/tests/`、短い C）: CRG の表の計算（PLL の値から rate）、DW MMC の command の組み立て、PLDA の ATR の窓の計算、PLDA の INTx の demux の表（status → pin → handler）、NVMe の INTx の mask/ack の順、YT8531 の拡張 register の値。
- 実機の試験は T1/T2（AGENTS.md「試験の担当 T1」）。判定は §5（#15）。

### 2.4 coherency の確かめ（M2・M3。時期と手順を 1 つに統一）

- **時期**: 最初の判定は **新しい Phase p023（仮）「coherency の確かめ」**（p022 = p013 の実機の確かめの後、DMA を使う device の Phase の前）。道具は最小の DW AXI DMAC の mem→mem の転送（p019 の DMAC の driver の先取り、cyclic は無し）。p014 の DW MMC は **PIO から始める**ので、p014・p015 は p023 を待たない（M8）。各 bus master の Phase（p014 の IDMAC、p020/p017 の xHCI・NVMe、p018 の GMAC、p019 の DMAC の cyclic）は、最初に同じ手順の短い版をその device で 1 回流す。表示は p011 で別に（§3.1）。
- **手順**（kernel の試験の command、shell の答えで判定: §5）:
  1. **scratch の領域**: `drv_dma_alloc_coherent` で 2 MiB の buffer を 2 つ（src・dst）。SD の root・FS は使わない（p014 の IDMAC の版は SD の scratch の partition（§3.3）だけを読み書きする）。
  2. **CPU→device（dirty の line）**: CPU が src に模様（address と回数の hash）を書き、**cache を flush しない**。src の先頭 256 KiB を最後にもう一度書いて、L2（2 MiB）に dirty の line が確実に残る様にする。
  3. **device→CPU（stale の clean の line）**: 転送の前に CPU が dst を全部読んで L2 に clean の line を載せておく。device に src → dst を転送させ、完了の後に CPU が dst を読む。coherent でなければ、(2) の dirty が device に見えないか、(3) の古い clean の line が読める。
  4. **逆向きの回**: device が書いた buffer に CPU が書き戻し、それを device がもう一度転送する回を 1 回。
  5. 全部一致すれば coherent。1 byte でも食い違えば非 coherent として下の予備へ。結果は bus master ごとに phase.md に記録（SDIO・GMAC・PCIe（PLDA の inbound の先の xHCI・NVMe）・AXI DMAC・表示）。
- **予備の案（非 coherent だった時、M3）**: 既存の DMA 層では **(i) だけ**が成り立つ（`drv_dma_sync_for_cpu/device` は何もせず（dma.c:706-725）、`drv_dma_map` は coherent の確保の中しか写さない（dma.c:633-660）。全部の DMA が `drv_dma_alloc_coherent` を通る）。版 1 の (ii)（streaming の sync）は取り下げた。
  - **(i) の具体化**: VF2 の BSP の `hal_pmem_map_uncached` を、**init で DDR の system port の別名（物理 + 0x4_0000_0000、0x4_4000_0000〜0x6_3FFF_FFFF の 8 GiB）全体を、root の予約の entry 448〜509 の中の 8 entry に 1 GiB の leaf で kernel 半分に写してから封じる**（design.md §2.5 の「init で確定して封じる」の約束の中に入る）。`hal_pmem_map_uncached(paddr)` は契約（hal.h:655-680）どおり範囲の line を ccache の FLUSH64 で書き出して捨ててから、その窓の address（窓の base + paddr − 0x4000_0000）を返す。`hal_pmem_unmap_uncached` は再び FLUSH64 で捨てる。hal.h は変えない。virt は今の UNSUPPORTED のまま（virt は coherent）。
  - **design.md §2.5（「448〜509 は invalid」「uncached の窓は無い」）と §2.1 の決定を変える** → §8 で Q1 に確認として返す。予備なので p023 が coherent と出れば不要。
  - 予備を使う時の LW: 2〜3（p023 の中、HAL の BSP の実装の変更）。

## 3. Phase ごとの設計

各 Phase: **既存**・**新規**・**HAL**・**DT**・**試験**・**依存**・**LW**（1 LW ≈ 15〜25 分、±50%）。

### 3.1 p011 framebuffer（M1 で改訂）

- **(1) 調べ**（実機、p005 の後）: mainline の U-Boot では GOP は出ない見込み（§1.4）なので、**配布の U-Boot を SD に置いて試す**（flash は書き換えない、#20）。DTB は §2.2 の `bootefi <img> <fdt>` で mainline の物を渡す。banner の版、HDMI の画、`bdinfo` の FB の行、loader の GOP の log、ExitBootServices の後に画が残るか、framebuffer の memory の EFI の型（boot services の型なら loader が handoff の予約に入れる）を見る。
- **(1) で GOP が出た時の code（p011 の中、承認の後）**:
  - **HAL**: **名前付きの handoff `riscv64.framebuffer.{phys,size,width,height,pitch,format}` の追加が要る（HAL の責務の追加、要承認）**。差分の案は [proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)（適用しない）。hal.h は不変。版 1 の「HAL: 無し」は誤りだった。
  - kernel: `visionfive2.c` が `kern_boot_framebuffer("riscv64.framebuffer", …)`（pmem.c の既存の関数）で読み、kernel 側の framebuffer の console（pcat の `graphics/backend.c`・rpi4 の読み手の形）を選ぶ。HAL の BSP は描かない（`hal-console.c` は HAL の console の binding で、framebuffer の console ではない）。
  - **表示の coherency の二案**（DC8200 が L2 を snoop するかは不明。前例: Pi 4 で描画の後の `hal_dcache_clean_range` が要った、guardrail:78）:
    - (α) framebuffer の範囲を **system port の別名（物理 + 0x4_0000_0000）で device の窓に写して**描く（`hal_space_map_device`、PMA が uncached。CPU の書き込みは直に DDR）。写す前に範囲を FLUSH64 で捨てる。描画は遅くなる（text の console なら足りる見込み）。
    - (β) cacheable の direct map で描き、描いた範囲ごとに `hal_dcache_clean_range`（VF2 は ccache の FLUSH64）。速いが描く側の全部に flush が要る。
    - **既定は (β)**（Pi 4 と同じ形）。console の描き手に flush の口が無ければ (α)。
  - **試験（p011 (1) に入れる）**: flush 無しで描いた画が出るか（出れば DC8200 は coherent で flush を省ける）、(β) で出るか、(α) で出るか、の 3 つを UART の command で切り替えて HDMI の画で見る。
- **(1) で出ない時**: (a) 配布の U-Boot を主にする（§2.2 を変えるのでユーザーの判断、#20）、(b) 自前の display の driver（DC8200・HDMI TX・VOUT の PMU・voutcrg。GPL の手順の参考を許す（§0.2）。大きい: 未見積、別の設計）。
- **依存**: p004・p005。HAL の承認（code の前）。**LW**: (1) 1〜2、code（handoff 6 行・読み手・coherency の切り替え）2〜4。

### 3.2 p013 周辺の基盤（code、板の前）と p022（仮）実機の確かめ（M6 で分けた）

- **方針**: U-Boot が残した状態に頼らず、各 driver が要る clock の gate と reset の解除を自分で行う（冪等）。pinmux は最初は読んで期待の値と照合し log に出すだけ、違えば設定する（GPIO 41 の card detect、PERST の 26・28）。
- **参照されない clock を止めない**（Minor 15）: driver が使わない clock・PD に触らない。特に VOUT の clock と PD_VOUT（止めると GOP の画が消える）、CPU・DDR・UART0 の clock。未使用の gate を落とす処理を書かない。
- **新規** `src/drivers/platform/visionfive2/`:
  - `vf2-crg.c`: SYS CRG（0x13020000）・STG CRG（0x10230000）・AON CRG（0x17000000）の gate・分周・mux と reset。API は「(CRG の phandle, 番号) → enable・disable・set_rate・get_rate」と「(phandle, reset の番号) → assert・deassert」。**rate は SYS SYSCON の PLL0〜2 の設定を読んで親の rate を計算する**（Minor 6。PLL は読むだけで変えない）。範囲は p014〜p019 が使う clock だけ。set_rate は SDCARD だけ（GMAC の TX は v1.3B の `starfive,tx-use-rgmii-clk` で不要、v1.2A で要れば p018 で足す: Minor 4）。
  - `vf2-pinctrl.c`: SYS IOMUX（0x13040000）と AON IOMUX（0x17020000）の GPIO と GPO/GPI の mux。GPIO の割り込みは使わない（card detect は poll）。
  - `vf2-syscon.c`: **SYS SYSCON**（SDIO の `starfive,sysreg`、PLL の読み取り）・**STG SYSCON**（PCIe の AXI の設定、offset は cfg の address で 0/1 を選ぶ）・**AON SYSCON**（gmac0 の `starfive,syscon = <&aon_syscon 0xc 0x12>`）の read-modify-write（Minor 3）。
  - **PMIC（AXP15060）と DW I2C の FDT 版は範囲から外す（既定、#22）**。OpenSBI が電源断に I2C5 を使う可能性（推測）とも競合しない（Minor 17）。
- **既存**: `drv_fdt`（`drv_fdt_find_compatible` の `after`、`drv_fdt_translate`）、rpi4-gpio.c の書き方。**HAL**: 無し。**DT**: `clocks`・`resets`・`assigned-clock-rates`・`*-gpios`・`starfive,syscon`・`starfive,sysreg`。
- **試験（p013、code）**: host の試験（PLL と分周から rate、gate・reset の bit の位置）。build（riscv64 の vf2 と virt、warning 0）。
- **p022（仮）実機の確かめ**（p005 の後）: 各 driver の clock の rate を PLL から計算した値と U-Boot の `clk dump` の値の照合（版 1 の「CRG の id の register を読む」は TRM で確かめていないので取り下げた: Minor 7）、GPIO 41 の card detect を挿抜で読む（shell の command の答え）、PERST の出力。
- **依存**: p013 は **p005 の skeleton（`visionfive2.c`・`platform/riscv64/vmunix.mk` の vf2 の枝・config）の merge** と p012。p022 は p013・p005（実機の login）。**LW**: p013 5〜8、p022 1〜2（実機の debug は別に 2〜4）。

### 3.3 p014 SD カード（M8 で改訂）

- **新規** `src/drivers/mmc/dw-mmc.c`（DW Mobile Storage Host）と `vf2-mmc.c`（JH7110 の glue: biu/ciu の clock、reset、`starfive,sysreg` の sample phase。sample phase の手順は Linux の参考を許す、§0.2）。
- **最初は FIFO の PIO**（OpenBSD dwmmc.c が PIO を持つ。fifo-depth 32）。**IDMAC は後**（p014 の後半、遅ければ Future Work）。IDMAC の descriptor の 32/64 bit は **HCON の DMA64 bit で決める**（obsd-dwmmc.c:346。版 1 の「64 bit の descriptor」は事実でなかった）。32 bit なら `drv_dma_device_create` の address_bits を 32 にし、届くのは DDR の下 3 GiB（0x4000_0000〜0xFFFF_FFFF）だけ（Minor 11）。IDMAC を入れる時は p023 の結果の後に、§2.4 の短い確かめを SD の scratch の partition で流す。
- **SD の protocol は dw-mmc の側に最小の SD の初期化を書く**（CMD0・8・ACMD41・2・3・9・7・ACMD6・CMD16、high speed の CMD6）。**rpi4-sdhci.c からの切り出しはしない（既定）**: rpi4-sdhci.c は PIO の driver で SD の protocol が SDHCI と絡み、arm64 の `DIRECT_BASE` を直に持ち（rpi4-sdhci.c:24・195）、Pi 4 は SD が root なので退行すると起動しない。共有化は Future Work（§7）。
- block の device として既存の MBR・GPT・FAT の層（`src/drivers/disklabel/`）に繋ぐ。速度は 50 MHz の 4 bit まで。
- **HAL**: 無し。**DT**: `starfive,jh7110-mmc` の `mmc1`（`mmc0` は無視）、`fifo-depth`、`cd-gpios`、`bus-width`、`max-frequency`、`starfive,sysreg`。
- **試験**: host（command の組み立て）。実機: card の識別、**scratch の partition（試験の image の最後に置く未使用の 64 MiB、image の build で確保）だけ**の raw の読み書きの一致、ESP の FAT を ls。
- **依存**: p013・p022。**p023 を待たない**（PIO）。**LW**: PIO 6〜9、IDMAC +2〜3。

### 3.4 p015 rootfs からの起動

- SD の image（ESP・root）で、kernel が root を SD の root partition から mount し、initrd 無しで init まで行く。
- **既存**: rpi4 の SD の root の道（`rpi4.c` の `KERN_DEV_SD` の root の選び方と `vfs.c`）。**新規**: visionfive2.c の root の選択（`KERN_DEV_SD` の形）。image の build（`plan/ws256/tests/` の config.mk、AGENTS.md の試験の image の作り方）。**HAL**: 無し。
- **受け入れの画面**（Minor 13）: ws.md の「画面で login」。p011 の GOP が出なければ p015 が止まるので、**UART の対話（#15 (A)）での受け入れを可にするか**をユーザーに聞く（#27）。既定: 可（画面は p011 が出た後に追って確かめる）。
- **試験**: 実機で 10 回の起動と login。**依存**: p014（p011 は画面の受け入れの時だけ）。**LW**: 3〜5。

### 3.5 p016 PCIe（virt の ECAM）・p021（仮）PCI core の host の INTx の hook・p020（仮）PLDA（B1）

- 版 1 の p016a/p016b を、規則の ID に合わせて **p016（virt の ECAM の FDT host、板の前）** と **p020（仮、PLDA の実機）** にし、PCI core の変更を **p021（仮）** に分けた（Minor 12。番号は Q1）。
- **p016（QEMU virt）**: `src/drivers/pci/pci-ecam-fdt.c`（`pci-host-ecam-generic`: config は ECAM、BAR は DT の `ranges` から host が割り当て、INTx は `interrupt-map` から PLIC の番号）。virt の PLIC の INTx は線ごとに別の source なので、既存の PCI core の `establish_intx`（vector = PLIC の番号を `kern_irq_register`）のままで動く。pci-brcmstb.c の host の形（`describe`・`start`・`publish`、bus_ops）を使う。MSI は無し（virt に AIA 無し）。
  - **新規の繋ぎ**（Minor 14）: `src/kern/platform/riscv-virt.c` は今 ns16550 だけを繋ぐ（144-160 行）。ECAM の host の登録、xHCI・usb-hid の登録、keyboard の input-bridge の繋ぎを足す（既存ではなく新規）。
  - **試験**: virt の `-device qemu-xhci -device usb-kbd`（T1、画面の判定。打鍵は QMP の `send-key`: tools の変更の担当は #28）。xHCI は INTx を許す（pci-xhci.c:6459-6464）ので attach する。NVMe は p017 の INTx の道の後。
  - **依存**: p007（userland の login）だけ。**p004 は要らない**（`-kernel` の直の起動、host が BAR を割り当てる: M6）。**LW**: 4〜6。
- **p021（仮）PCI core の host の INTx の hook**（B1）:
  - 問題: PLDA は INTA〜D を 1 本の PLIC の線にまとめ、host が `ISTATUS_LOCAL` を読んで振り分けと W1C の ack をする。今の `establish_intx`（pci.c:3037）は vector をそのまま `kern_irq_register`（→ `hal_irq_register`、riscv64 の番号は PLIC の source）に渡し、`drv_pci_bus_ops`（pci.h:126-177）に host が割り込みに加わる hook が無い。host が線 56 を登録すると core の登録と EBUSY で衝突し、渡すだけだと誰も ack しない。
  - **設計**: `drv_pci_bus_ops` に省略可能な hook を足す（`include/drivers/pci/pci.h`、driver の内部の API で hal.h は変えない）。
    - `intx_establish(context, device, irq, dispatch, dispatch_arg)`: core の `establish_intx` は、hook がある時は `kern_irq_register` の代わりにこれを呼ぶ。host は pin（host が `allocate_irqs` で `irq->private_data` に入れた pin）ごとに `dispatch`（core の `pci_intx_dispatch`、共有の handler を順に呼ぶ）を表に置き、その pin を `IMASK_LOCAL` で unmask する。host は自分の PLIC の線を attach の時に 1 回だけ `kern_irq_register` する（core はその線に触らない）。
    - `intx_disestablish(context, device, irq)`: core の line の解除（pci.c:3550-3558 の `kern_irq_mask`・`kern_irq_unregister`）で、hook がある時は代わりに呼ぶ。host は pin を mask して表から外す。
    - `intx_mask` / `intx_unmask(context, irq)`: core が `kern_irq_mask/unmask(line->irq)` を呼ぶ所（pci.c:3095 など）で、hook がある時は代わりに呼ぶ（pin の `IMASK_LOCAL`）。
    - host の割り込みの処理: `ISTATUS_LOCAL` を読み、立った INTx の pin の `dispatch` を呼び、**level なので handler の後に** W1C で ack（OpenBSD の「Ack INTx late」と同じ順）。event・MSI の bit は ack して捨てる。
  - hook が NULL の host（pci-brcmstb.c、amd64 の host、pci-ecam-fdt.c）は今の道のまま（`ops->intx_establish != NULL` の分岐だけ）。
  - **試験**: host の試験（fake の bus_ops で core の establish/disestablish/mask の呼び分け）。**build: amd64（pcat）・arm64（Pi 4）・riscv64（virt・vf2）の vmunix を warning 0**。**回帰（T1 に依頼）: Pi 4 の xHCI（brcmstb の INTx の道。USB keyboard で login、boot-test.sh の画面）、amd64 の QEMU の xHCI と NVMe**（core の分岐が既存の道を変えない事）。
  - **依存**: p012 だけ（板の前に書ける）。**LW**: 3〜5（T1 の時間は別）。
- **p020（仮）PLDA（実機）**: `src/drivers/pci/pci-plda.c`（apb の register、ATR の outbound（cfg・32 bit・64 bit）と inbound（DDR）、link の train、PERST の GPIO、PHY（stfpciephy: mode の設定）、**INTx は p021 の hook で**、STG SYSCON は cfg の address で 0/1 を選ぶ）。手順は OpenBSD stfpcie.c・stfpciephy.c、値は Value(s) referenced。
  - **MSI は使わない**（B2 の選択、§3.6）。PLDA の MSI の demux は Future Work（§7）。
  - USB: 既存の pci-xhci・usb-hub・usb-hid・hid-input をそのまま（INTx で attach）。VL805 なら firmware の load は無い見込み（未確認）。
  - **HAL**: 無し。**DT**: `starfive,jh7110-pcie`（reg の cfg・apb、ranges、interrupts、interrupt-map、`perst-gpios`、`phys`、`starfive,stg-syscon`、clocks・resets）。
  - **試験**: host（ATR の窓、demux の表）。実機: link up、列挙の答え、USB keyboard で login。最初に §2.4 の短い確かめを xHCI で 1 回。
  - **依存**: p013・p022・p021・p016（ECAM で host の形を確かめた後）・p023。**LW**: 8〜12。

### 3.6 p017 NVMe（B2・M4 で改訂）

- **B2 の選択: pci-nvme に INTx の道を足す**（PLDA の MSI を host hook で出す案は選ばない）。理由: (1) virt の p016 も INTx だけなので、MSI の案では virt で NVMe が試せない。(2) PLDA の MSI は OpenBSD も `#ifdef notyet`、Pi 4 の brcmstb も MSI の受け手が無い（pci-brcmstb.c:1510）。INTx の道は両方の板と virt で使える。(3) MSI の hook は PCI core の `kern_irq_register_msi` の道（pci.c:3209）を host に渡す大きな変更になる。
- **新規（pci-nvme.c）**: `drv_pci_device_allocate_irqs` の許可に INTx を足す（今は MSIX|MSI、pci-nvme.c:1738-1740。MSI-X・MSI が取れる時はそちらが先）。INTx の時の処理: 割り込みの入口で **INTMS で mask → CQ を処理 → INTMC で unmask**（共有の level の線なので、自分の CQ に何も無ければ「自分の割り込みでない」を返す）。今の INTMS/INTMC の扱い（1715・1769-1772・2874-2880 行: MSI-X の前に mask を外し、MSI-X の後は触らない）は MSI-X・MSI の時のまま。
- **新規（visionfive2.c、M4）**: NVMe の root は rpi4 の道（`KERN_DEV_SD` だけ、`include/kern/boot.h` に NVMe の class は無い）ではなく **pcat の形**: `boot0=UUID=` の読み取り、`probe_fixed_disks`、`wait_for_boot_disk`（pcat.c:478-530・790-800）と `drv_pci_nvme_driver_register`・`drv_pci_nvme_probe_namespaces` の呼び出しを visionfive2.c に足す。
- **試験**: host（INTx の mask/ack の順）。QEMU virt（`-device nvme`、p016 の上、INTx）で読み書きと mount。**回帰（T1 に依頼）: amd64 の QEMU の NVMe（MSI-X の道が変わらない事）と、arm64 の build**。実機: M.2 の SSD の識別・読み書きの一致・NVMe の root で起動。
- **依存**: virt の部分は p016、実機は p020・p015（root の選び方の土台）。**LW**: INTx 2〜3、NVMe の root 3〜4、試験 1 → 6〜8（実機の debug は別）。

### 3.7 p018 GbE と PHY

- **新規** `dw-eqos.c`（Ethernet QoS 5.20: DMA の descriptor の ring、MTL、MAC、MDIO）、`vf2-ethernet.c`（AHB・AXI・PTP・TX・RX の clock、RGMII の mode の **AON SYSCON** の bit、reset。TX の set_rate は `starfive,tx-use-rgmii-clk` が無い時だけ）、`yt8531.c`（**p018 の最初に MDIO で PHY の id を読んで型を決める**。拡張 register の RGMII の delay と `motorcomm,*`。手順は Linux の参考を許す、§0.2）。
- 既定は **gmac0 だけ**（v1.2A の gmac1 は RMII）。
- **HAL**: 無し。**DT**: `starfive,jh7110-dwmac`、`phy-handle`・`phy-mode`・`mdio`、`rx/tx-internal-delay-ps`、`motorcomm,*`、`starfive,tx-use-rgmii-clk`、`starfive,syscon`、`assigned-clock-parents`。
- **試験**: 最初に §2.4 の短い確かめを GMAC の descriptor で 1 回。実機: link up、DHCP、host から ssh（§5 の (C)）。
- **依存**: p013・p022・p023。**LW**: 10〜14。

### 3.8 p019 sound

- **道（既定、#23）: 3.5 mm の PWMDAC**。
- **新規** `vf2-pwmdac.c` と `src/drivers/dma/dw-axi-dmac.c`（cyclic の転送。mem→mem の最小の部分は p023 で先に作る）。**PWMDAC の node に `interrupts` が無いので PIO は timer の poll になり、DMA は事実上必須**（版 1 の「PIO で足りれば DMA を省く」は取り下げた: Minor 2）。DMAC の 4 本の channel から 1 本を取り、PWMDAC の handshake 22 を割り当てる（Minor 1）。手順は Linux の参考を許す（§0.2）。
- 既存の `src/drivers/audio/audio.c`（`drv_audio_register`）に繋ぐ。48 kHz を既定。
- **HAL**: 無し。**DT**: `starfive,jh7110-pwmdac`、`dmas`、`starfive,jh7110-axi-dma`（`dma-channels`）、clocks・resets、`pinctrl-0`（GPO 28・29 を GPIO 33・34）。
- **試験**: 最初に §2.4 の短い確かめを DMAC の cyclic で 1 回。判定は #24。
- **依存**: p013・p022・p023。**LW**: 7〜11（JH7110 の DMAC に固有の差は推測で、上限寄り）。

### 3.9 p023（仮）coherency の確かめ

- §2.4 の手順を、最小の DW AXI DMAC の mem→mem の転送（`dw-axi-dmac.c` の先取り）で流す。結果で予備の (i) の要否を決め、要れば同じ Phase で (i) を実装する（HAL の BSP の実装の変更、hal.h 不変、design.md §2.5 の改訂の Q1 の確認の後）。
- **依存**: p013・p022。**LW**: 3〜4（(i) を実装する時 +2〜3）。

## 4. 依存と並行（M6 で直した）

### 4.1 Phase の一覧（仮の番号）

| ID | 中身 | 依存 | LW |
| --- | --- | --- | --- |
| p011 | framebuffer: (1) 調べ、code（HAL の承認の後） | p004・p005、HAL の承認 | 3〜6 |
| p013 | 周辺の基盤の code（板の前） | p005 の skeleton の merge・p012 | 5〜8 |
| p014 | SD（PIO、後で IDMAC） | p013・p022 | 6〜12 |
| p015 | SD の root で起動 | p014 | 3〜5 |
| p016 | virt の ECAM の FDT host と virt の繋ぎ | p007 | 4〜6 |
| p017 | NVMe の INTx の道と NVMe の root | p016（virt）、p020・p015（実機） | 6〜8 |
| p018 | GbE と PHY | p013・p022・p023 | 10〜14 |
| p019 | sound（PWMDAC と DMAC の cyclic） | p013・p022・p023 | 7〜11 |
| **p020（仮）** | PLDA の host（実機） | p013・p022・p021・p016・p023 | 8〜12 |
| **p021（仮）** | PCI core の host の INTx の hook | p012 | 3〜5 |
| **p022（仮）** | p013 の実機の確かめ | p013・p005 | 1〜2 |
| **p023（仮）** | coherency の確かめ（要れば予備 (i)） | p013・p022 | 3〜7 |

- 合計: **59〜96 LW**（実機の debug は別に p022・p020・p018 で各 2〜8）。版 1 の 47〜72 から、hook（p021）、NVMe の INTx と root、coherency の確かめ、DMAC、framebuffer の code を足した。p011 (b)（自前の display）は含まない。

```
p004 → p005 ─┬→ p011
             └→ p013(code) → p022(実機) ─┬→ p014 → p015 ──────────┐
                                         └→ p023 ─┬→ p018          │
                                                  ├→ p019          │
p012 → p021 ──────────────────────────────────────┼→ p020 → p017(実機)
p007 → p016 ──────────────────────────────────────┘
          └→ p017(virt)
```

### 4.2 並べる順の案（P1・P2）

| 段 | P1 | P2 | 理由 |
| --- | --- | --- | --- |
| 1（板の前） | p016（p007 の後）→ p017 の virt の部分、p021 | p004 → p005 | どれも QEMU・host だけ。p021 は p012 だけに依る |
| 2 | p013 の code（p005 の skeleton の merge の後） | p005 の実機、p011 (1) | p013 は p005 の skeleton の上 |
| 3 | p022・p023（実機）→ p020 → p017 の実機 | p014 → p015 | p014 は PIO なので p023 を待たない |
| 4 | p018 | p019 | |
| 最後 | p006（規約、Q1） | | |

- **P1・P2 が共有する file**（M6）: `src/kern/platform/visionfive2.c`（init の表・root の選び方）、`platform/riscv64/vmunix.mk`（vf2 の object の一覧）、config と menuconfig の項目、`vf2-crg.c` の clock の表（各 Phase が使う clock を足す）。各 Phase は自分の行だけを足す。同時に 2 つの Phase が同じ表を変える段 3・4 は、Q1 が merge を 1 つずつ入れて衝突を解く。
- 実機の試験は板が 1 枚なので T1/T2 が順に流す。

## 5. 実機の判定（#15）と接続（#10）

### #15 実機の判定

| 案 | 内容 | 使える時期 | 利点・欠点 |
| --- | --- | --- | --- |
| (A) serial の対話 | `plan/tools/guest/serial.py` に実機の tty を足し、`login:` を待って command を打ち、**答えの文字列**で判定（log を読まない）。WS256 の実機に限る例外 | p005 から | 画面が無くても判定できる。新しい用法でユーザーの明示の許可が要る |
| (B) HDMI の capture | host に UVC の HDMI capture を繋ぎ、boot-test.sh と同じく画面を撮る | p011 で画が出た後 | zedBSD の「画面で判定」に一番近い。機器はユーザーの用意 |
| (C) SSH | p018 の後に `guest.sh` | p018 の後 | 速い |

既定: p005〜は (A)、画が出たら (B)（機器があれば）、p018 の後は (C) を足す。virt は今までどおり画面。

### #10 実機の接続

| 項目 | 選択肢 | 既定 |
| --- | --- | --- |
| console | (a) 3.3 V の USB-UART を 40 pin の 6・8・10 に、115200、(b) なし | (a)、udev の by-id で名前を固定 |
| 電源 | (a) SwitchBot の plug（`switchbot.py cycle`）、(b) 手で | (a) |
| 媒体の更新 | (a) SD を host で書いて差し替え、(b) SD の mux、(c) U-Boot の TFTP（mainline の U-Boot の `DWC_ETH_QOS`）、(d) NVMe | 開発は (c)、受け入れは (a)（p015）と (d)（p017） |
| LAN | 試験の network に、DHCP の address を固定 | p018 と (c) に要る |
| HDMI | monitor か capture | p011 の時 |

## 6. ユーザーに聞く判断（Q1 経由）

| # | 問い | 選択肢 | 既定 | 理由 |
| --- | --- | --- | --- | --- |
| 15 | 実機の判定の方法 | (A) serial の対話 / (B) HDMI の capture / (C) SSH、組み合わせ | (A)→(B)→(C)（§5） | 画面が出るのは p011 の後、SSH は p018 の後。(A) は新しい例外なので許可が要る |
| 10 | 実機の接続 | §5 の表 | §5 の既定 | TFTP で SD の差し替えを減らす |
| 2 | 板の版（v1.2A・v1.3B）と memory | — | gmac0 だけを支える | v1.2A の gmac1 は RMII |
| 20 | U-Boot の版と flash の書き換え | (a) mainline 2025.01 を SD に（主）、配布の 2021.10 は p011 (1) の試しだけ / (b) 配布を主に / (c) flash を書き換える | (a)、flash は書き換えない | 決定 2（DTB は U-Boot から）と mainline の binding が合う。配布は GOP の試しにだけ要る（M5） |
| 21 | DTB の出どころ（**決定 2 に関わる**） | (a) U-Boot の control DT（決定 2 のまま）/ (b) 別の DTB を U-Boot の `bootefi <img> <fdt>` で（fixup は U-Boot が当てる見込み）/ (c) loader が差し替える | (a)、配布の U-Boot の試しの時だけ (b)。(c) は取り下げ | (c) は U-Boot の fixup（boot-hartid・memory・reserved-memory・MAC）を全部消す（M5） |
| 22 | PMIC（AXP15060・I2C）を外すか | 外す / p013 に入れる | 外す | 要るのは eMMC と CPU の電圧だけ。OpenSBI の I2C5 との競合も避ける |
| 23 | sound の道 | 3.5 mm の PWMDAC / HDMI の audio | PWMDAC | HDMI の audio は自前の display（p011 (b)）が要る |
| 24 | sound の判定 | ユーザーの耳 / host の line-in の録音 | 耳（試験の一覧に載せる） | 機器が要らない |
| 25 | coherency の推定の採否と予備 | 推定（coherent）で設計し p023 で確かめる。予備は (i) system port の別名の窓 | そのとおり | Linux の riscv の既定と、Linux・OpenBSD とも JH7110 で cache 操作をしない事（§1.2）。(i) は design.md §2.5 を変える（§8） |
| 26 | p016 の分け方 | 版 1 の p016a/b を p016（virt）と p020（PLDA）に、PCI core の hook を p021 に | そのとおり | 板の前に進められ、core の変更の回帰を分けられる |
| 27（新） | p015 の受け入れを UART の対話で可にするか | 可（画面は p011 の後に追う）/ 不可（p011 を待つ） | 可 | GOP が出ないと p015 が止まる（Minor 13） |
| 28（新） | tools の変更の担当と許可 | serial.py の実機の tty（#15 (A)）と QMP の `send-key`（p016）を、(a) その Phase の担当が変える許可 / (b) Q1 が変える | (b) Q1（tools は共有） | #18 の許可は p009 の 3 file だけ（Minor 16） |
| 29（新） | HAL の承認: `riscv64.framebuffer.*` の名前付き handoff | 承認 / 却下（p011 は画無しで進める） | 承認（p011 の code の前） | [proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)、hal.h 不変（M1） |
| 30（新） | p021 の `drv_pci_bus_ops` の hook（hal.h 不変、PCI の内部 API） | 進める / 別の形 | 進める（HAL ではないので承認は要らない見込み。amd64・Pi 4 と共有の pci.c を変えるので知らせる） | B1 |
| 31（新） | pci-nvme の INTx の道（amd64 の NVMe と共有の file） | INTx を足す（B2 の選択）/ PLDA の MSI を host hook で | INTx を足す | §3.6 の理由 |

## 7. 未確認と Future Work

- 未確認: VL805 の型番と USB・M.2 がどちらの PCIe か。JH7110 の DMA と DC8200 の coherency。GOP が出るか、ExitBootServices の後に画が残るか。U-Boot の `bootefi <img> <fdt>` が渡された DTB に fixup を当てるか。配布の U-Boot の `RISCV_EFI_BOOT_PROTOCOL`。板の版・memory。PHY の id と v1.2A の PHY。PWMDAC の register。`ISTATUS_LOCAL` が latch か level か。OpenSBI の電源断の実装と I2C5。PLDA の inbound の窓の制約。DW MMC の HCON の DMA64。
- Future Work（Q1 が future-work.md に足すかを決める）: SD の protocol の rpi4-sdhci.c との共有化（M8）、PLDA の MSI の demux（B2）、DW MMC の IDMAC を p014 で入れなかった時、eMMC と PMIC。

## 8. Q1 への依頼

- **HAL の承認**: [proposed/hal-riscv64-framebuffer.md](proposed/hal-riscv64-framebuffer.md)（#29）。
- **design.md の直し**（この文書からは直さない）: (1) §1.6 の「JH7110 の DMA は非 coherent」→ §1.2 の推定に。(2) §2.5・§2.1 の「448〜509 は invalid」「uncached の窓は無い」に、予備 (i)（system port の別名 8 GiB を init で 1 GiB の leaf で写して封じる）を入れてよいかの確認（#25、予備なので p023 で coherent なら不要）。(3) §3.2 の起動の switch（`MSEL[1:0]: 10 SD、00 flash`）を RGPIO_1/0 の 01 SD・00 flash に。(4) §3.2 の DTB の扱いに §2.2（`bootefi <img> <fdt>`）を足す。
- **番号**: p020〜p023 の仮の番号を Master の next で決め、ws.md の表に行を足す（p016 の行の中身を virt の ECAM に変える）。
- **tools**（#28）: serial.py の実機の tty、QMP の `send-key`。
- `build/tmp/ws256-docs/starfive,jh7110-crg.h` の片付け。

## 9. レビュー 1 への対応

| 指摘 | 対応 | 所 |
| --- | --- | --- |
| B1 PLDA の INTx を core が配れない | 直した: `drv_pci_bus_ops` に省略可能な INTx の establish・disestablish・mask/unmask の hook（hal.h 不変）、host が線を 1 回登録し `ISTATUS_LOCAL` で demux、handler の後に W1C の ack。p021（仮）に分け、amd64・arm64・riscv64 の build と Pi 4 の xHCI・amd64 の xHCI/NVMe の回帰を T1 に。LW 3〜5 | §3.5 |
| B2 pci-nvme が MSI 必須 | 直した: pci-nvme に INTx の道（INTMS/INTMC）を足す案を選んだ（理由 3 つ）。PLDA は INTx だけと明記、MSI は Future Work。amd64 の NVMe の回帰を計画 | §3.6、§7 |
| M1 framebuffer の HAL の責務と表示の cache | 直した: `riscv64.framebuffer.*` の差分の案を proposed/ に置いた（適用しない）。kernel 側の console。coherency の二案 (α)(β) と p011 (1) の 3 通りの試験 | §3.1、proposed/hal-riscv64-framebuffer.md |
| M2 coherency の手順と時期 | 直した: p023（仮）に統一し、dirty の line・stale の clean line・両方向・bus master ごと・scratch の領域の手順。根拠に Linux の `ARCH_DMA_DEFAULT_COHERENT`・`of_dma_is_coherent` と OpenBSD sfcc.c を取得して足した | §1.2、§2.4、§0.1 |
| M3 予備の案 | 直した: (ii) を取り下げ (i) だけを具体化（system port の別名 8 GiB を init で写して封じ、FLUSH64 の後に返す）。uncached の根拠を Table 2-5 の `RW A`（C 無し）に直した。design.md §2.5 の改訂は Q1 に確認として返す | §1.2、§2.4、§8 |
| M4 NVMe の root の過小 | 直した: pcat の形（`boot0=UUID=`・`probe_fixed_disks`・`wait_for_boot_disk`・NVMe の登録）を visionfive2.c の新規に。LW を足した | §3.6 |
| M5 U-Boot と DTB | 直した: mainline の U-Boot を主、配布は p011 (1) の試しだけ。loader の差し替えを取り下げ、`bootefi <img> <fdt>`。決定 2 に関わる事を #21 に明記 | §2.2、§6 #20・#21 |
| M6 依存と並行の表 | 直した: p013 を code（p005 の skeleton の merge に依る）と p022（実機）に分けた。p016 は p004 不要で p007 の後。共有の file を全部挙げた | §3.2、§4 |
| M7 GPL の手順の出典 | 決着として書いた: ユーザーの決定（2026-10-19、クリック）「手順の参考を許す」。部品ごとの出典の表 | §0.2 |
| M8 sd-card の切り出し | 直した: 既定を「切り出さない」に、DW MMC は PIO から。共有化は Future Work | §3.3、§7 |
| Minor 1 DMAC の 22 | 直した: handshake の番号、channel は 4 本 | §1.1、§3.8 |
| Minor 2 PWMDAC に interrupts 無し | 直した: DMA は必須、PIO の案を取り下げ、LW を 7〜11 に | §3.8 |
| Minor 3 AON SYSCON | 直した: vf2-syscon.c に足した | §3.2 |
| Minor 4 TX の set_rate | 直した: `tx-use-rgmii-clk` が無い時だけ | §3.2、§3.7 |
| Minor 5 STG SYSCON の offset | 直した: 事実の表と p020 に | §1.1、§3.5 |
| Minor 6 PLL | 直した: SYS SYSCON の PLL0〜2 を読んで rate を計算 | §3.2 |
| Minor 7 CRG の id の register | 直した: 取り下げ、U-Boot の `clk dump` との照合に | §3.2 |
| Minor 8 PREBOOT の `usb start` | 直した: mainline だけ、配布は `run chipa_set_uboot;` | §1.4 |
| Minor 9 YT8531 の名前 | 直した: 推定と明記、p018 の最初に PHY の id を読む | §1.1、§3.7 |
| Minor 10 起動の switch | 直した: design.md の直しとして Q1 に | §1.1、§8 |
| Minor 11 address_bits | 直した: HCON の DMA64 で決め、32 bit なら address_bits 32 | §3.3 |
| Minor 12 ID の形 | 直した: p016・p020〜p023（仮、番号は Q1） | 冒頭、§4.1、§8 |
| Minor 13 p015 の画面 | 直した: UART での受け入れを #27 で聞く | §3.4、§6 |
| Minor 14 riscv-virt.c の繋ぎ | 直した: 新規として p016 に | §3.5 |
| Minor 15 参照されない clock | 直した: 止めない事を明記（VOUT・PD_VOUT ほか） | §3.2 |
| Minor 16 tools の変更の担当 | 直した: #28 で聞く、既定は Q1 | §6、§8 |
| Minor 17 OpenSBI と I2C5 | 直した: #22 の理由に足した | §1.1、§3.2、§6 |
| 直さなかった物 | 無し（全部を当てた）。ただし推測の部分（`ISTATUS_LOCAL` の latch、bootefi の fixup、DC8200 の snoop、DMA の coherency）は実機まで未確認のまま | §7 |
