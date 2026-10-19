# WS256 VisionFive 2 の周辺の設計（p012、初版）

- Phase: [ws256-p012](phase012/phase.md)、Queue q1270-i01、担当 P1、2026-10-19。
- 範囲: p011（framebuffer の調べ）と p013〜p019（clock・reset・pinctrl/GPIO・PMIC、SD、rootfs からの起動、PCIe と USB・keyboard、NVMe、GbE、sound）の設計。code は書かない。HAL の API の差分は案だけで、適用しない。
- 前提: [design.md](design.md)（改訂 2）の HAL の芯・起動の道・ユーザーの決定 1〜12。この文書は design.md の §6 の p011〜p019 を詳しくする。
- **design-reviewer にまだ通していない**（Q1 の指示で P1 は子のエージェントを起動しない）。ws.md の p012 の行は「design-reviewer に通す」なので、Q1 が回す。

## 0. 出典

### 0.1 取得した物（2026-10-19、P1 の worktree の `build/tmp/ws256-docs/`、git に入れない）

| file | URL | SHA-256 | license・扱い |
| --- | --- | --- | --- |
| JH7110_TRM_StarFive_Preliminary_V2.pdf | https://doc-en.rvspace.org/JH7110/PDF/JH7110_TRM_StarFive_Preliminary_V2.pdf | 6ad0dcc58948a69daf74d17899d1e83bc05292523edaa613c0c883c590c88c92 | StarFive の公開の文書。memory map・割り込み・CRG・SYSCON・IOMUX・PMU の事実の第一の出典 |
| JH7110_Datasheet.pdf | https://doc-en.rvspace.org/JH7110/PDF/JH7110_Datasheet.pdf | 5d3eb13e93a97e819464d61343a81fbc286509f77cfcf64688d11d4c3217e80f | 同上。機能の一覧（PHY の型番など） |
| VisionFive2_Datasheet.pdf | https://doc-en.rvspace.org/VisionFive2/PDF/VisionFive2_Datasheet.pdf | d5bfbf769f4a3b7bf624657903393ac1e12a937c30f1e9c355cda07fe42215e0 | 同上。板の構成・40 pin・boot の switch |
| jh7110.dtsi | https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/riscv/boot/dts/starfive/jh7110.dtsi | 53dc95b66a6fd91cbe0e87f28b6eed295766be30d6146e4cb59e4070d3a289ab | GPL-2.0 OR MIT。値（address・IRQ・clock/reset の番号） |
| jh7110-common.dtsi | …/v6.12/arch/riscv/boot/dts/starfive/jh7110-common.dtsi | a1fd9f5571295c3e398257befa23b5c4bd72d597be7e9ccdcc03af31f240013c | 同上。VF2 の板の配線（PMIC・SD・PCIe の PERST・sound） |
| jh7110-starfive-visionfive-2.dtsi | …/jh7110-starfive-visionfive-2.dtsi | 4847a7997f914bdf6b124220da841ebdebe9c8d103de93e357e92eab9cff8253 | 同上 |
| jh7110-starfive-visionfive-2-v1.2a.dts / -v1.3b.dts | …/-v1.2a.dts、…/-v1.3b.dts | fe9ee6a3…3c067 / d86cf0fc…60b9 | 同上。版ごとの GbE の差 |
| jh7110-pinfunc.h、clock-jh7110-crg.h、reset-jh7110-crg.h | …/v6.12/arch/riscv/boot/dts/starfive/jh7110-pinfunc.h、…/include/dt-bindings/clock/starfive,jh7110-crg.h、…/include/dt-bindings/reset/starfive,jh7110-crg.h | f380f601…e5df、55df963e…f4、7db50541…86 | GPL-2.0 OR MIT（binding の header）。clock/reset の番号 |
| linux-v6.12-sifive_ccache.c | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/cache/sifive_ccache.c | 306f635a9e7bb7c6c6fee20d13327c0aac5f19269d05a6ffd4612d3debecb770 | GPL-2.0。読むだけ（§1.2 の coherency の判断） |
| u-boot-vf2-defconfig | https://raw.githubusercontent.com/starfive-tech/u-boot/JH7110_VisionFive2_devel/configs/starfive_visionfive2_defconfig | e051baa92489f53dddd0b5dfd4965fe0e0d0772985850b040b349f5266ed3272 | GPL-2.0+。StarFive の配布の U-Boot の構成 |
| u-boot-mainline-vf2-defconfig | https://raw.githubusercontent.com/u-boot/u-boot/v2025.01/configs/starfive_visionfive2_defconfig | 25b00e347cc7c40265df192fd59095bda1c3219f35e9d1668e2e8c5eb7880f91 | 同上。mainline の U-Boot の構成 |
| README.md（StarFive の SDK） | https://raw.githubusercontent.com/starfive-tech/VisionFive2/JH7110_VisionFive2_devel/README.md | d9ef3d14363c59f4dbfba54a24fc4dbf881b8668c2987569cfbe4dc28a06ea1a | SDK の U-Boot の版（2021.10） |
| obsd-stfclock.c・stfpcie.c・stfpciephy.c・stfpinctrl.c（OpenBSD `sys/arch/riscv64/dev/`）、dwmmc.c・if_dwqe_fdt.c・axppmic.c（`sys/dev/fdt/`）、dwqe.c（`sys/dev/ic/`）、ytphy.c（`sys/dev/mii/`） | https://raw.githubusercontent.com/openbsd/src/master/<path>（取得の時の master は 859883f17cb8e20088fece5398547e4264ad30b3、2026-10-19T03:16:39Z） | `build/tmp/ws256-docs/SHA256SUMS` に全部（例 stfpcie.c 0465561ccf7f3e26bb5ebec84e6799d0c509d2273341a43933a6bce88c212ff1） | ISC。JH7110 の clock・PCIe・pinctrl の手順の参考。code は写さない |

`SHA256SUMS` に全部の file の SHA-256 がある。directory に `starfive,jh7110-crg.h` が残る（clock と reset の header を同じ名前で取った時の上書きの残り、中身は reset の header と同じ。rm はしないので Q1 が片付ける）。

### 0.2 参照の扱い

- ユーザーの決定 6（「WS245 と同じ」）と [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」に従う: 値（register の offset・bit・clock/reset の番号・IRQ）は block ごとに「Value(s) referenced:」、手順は「Procedure referenced:」で、block の直前の複数行の comment に書く。file の先頭には出典を書かない。code・comment・構造・symbol の名前は写さない。
- **TRM が書かない IP の register**: JH7110 の TRM は SoC の system の部分（memory map、割り込み、SYS/STG/AON CRG、SYSCON、IOMUX、PMU）だけを書き、他社の IP（Synopsys の DesignWare MMC・I2C・Ethernet QoS、PLDA の PCIe、Cadence の USB、PWMDAC）の register の説明は無い（2026-10-19 に TRM の目次と本文を grep して確かめた: `PWMDAC` は clock・reset・pinmux の行だけ、`DesignWare`・`QoS` は 0 件）。これらの register の出典は、優先の順に (1) OpenBSD の ISC の driver（JH7110 の clock・PCIe・pinctrl・DW MMC・DW QoS がある）、(2) U-Boot・Linux の GPL の driver の値（Guardrail で値の引用は可）、とする。
- Linux・U-Boot の driver は読むだけで、tree に入れない。

## 1. 確かめた事実

### 1.1 板の構成（VisionFive2_Datasheet、Linux v6.12 の DTS）

| 機能 | 事実 | 出典 | 未確認 |
| --- | --- | --- | --- |
| SD | TF card の slot。SoC の SDIO1（`mmc1`、0x16020000、`starfive,jh7110-mmc`、IRQ 75、4 bit、`cap-sd-highspeed`、`no-sdio`・`no-mmc`、card detect は sysgpio 41（active low）、clock は SYSCLK_SDIO1_AHB（biu）と SDCARD（ciu、50 MHz に設定）、`fifo-depth = 32`） | DTS、TRM の memory map | UHS（1.8 V）は DTS に無い → 3.3 V の high speed（50 MHz）まで |
| eMMC | socket（`mmc0`、0x16010000、8 bit、HS200、`non-removable`、vqmmc は PMIC の aldo4） | DTS | eMMC は範囲外（ws.md「eMMC は未定」） |
| SD の controller の IP | U-Boot は `CONFIG_MMC_DW`・`MMC_DW_SNPS`（Synopsys DesignWare の Mobile Storage Host） | 両方の U-Boot の defconfig、OpenBSD の dwmmc.c が jh7110 を扱う | register の公開の databook は無い |
| PCIe | 2 本の PCIe 2.0 x1（pcie0 cfg 0x9_4000_0000・apb 0x2b000000、pcie1 cfg 0x9_c000_0000・apb 0x2c000000）。IP は PLDA（TRM の `u0_plda_pcie`）。PHY は pciephy0/1（0x10210000/0x10220000）。MSI は controller の中、INTx は controller の中の intc。PERST は sysgpio 26（pcie0）・28（pcie1）。memory の窓は 32 bit 0x30000000/0x38000000（各 128 MiB）と 64 bit の prefetch 0x9_0000_0000（pcie0）・0x9_8000_0000（pcie1） | DTS、TRM の memory map | ws.md の「PLDA XpressRICH」の XpressRICH の名前は公開の文書に無い（TRM は PLDA とだけ書く） |
| USB 3.0 ×4 | 「4 × USB 3.0 ports (multiplexed with a PCIe 2.0 1x lane)」。U-Boot は `USB_XHCI_PCI`（両方）と `USB_KEYBOARD`（mainline）を持ち、PREBOOT が `usb start` → USB の port は PCIe の先の xHCI | VF2 datasheet、U-Boot の defconfig | **VL805 の名前は取得した公開の文書のどれにも無い**（回路図は未取得）。どちらの PCIe に付くかも未確認。p016 の最初に U-Boot の `pci` command で vendor・device を読んで確かめる（VL805 なら 0x1106:0x3483、`rpi4-pcie.c` の `RPI4_VL805_IDENTITY`） |
| SoC の USB | `usb0`（0x10100000、Cadence USB3、`dr_mode = "peripheral"`）は USB-C の OTG の側 | DTS、TRM（`u0_cdn_usb_xhci`） | 範囲外（keyboard は PCIe の xHCI の port に挿す） |
| M.2 | M-Key、PCIe 2.0 x1 | VF2 datasheet | どちらの PCIe かは未確認（p016 で `pci` を読む） |
| GbE | RJ45 ×2。gmac0（0x16030000）・gmac1（0x16040000）、`snps,dwmac-5.20`（Synopsys の Ethernet QoS）、RGMII。PHY は Motorcomm の YT8531（JH7110 datasheet の対応 PHY は YT8521DH/DC・YT8531DH/DC）。DTS の v1.3B は両方 RGMII で `motorcomm,*` の調整、**v1.2A の gmac1 は RMII**（PHY の型も違う可能性） | DTS、JH7110 datasheet §2.6.3 | 板の版（#2）。v1.2A の PHY の型番。OpenBSD の ytphy.c は YT8521 の model しか持たない（YT8531 の id は未確認） |
| PMIC | X-Powers AXP15060、I2C5（0x12050000）の address 0x36。dcdc1 = 3.3 V（always-on）、dcdc2 = CPU の電圧、aldo4 = eMMC の vqmmc | DTS（`jh7110-common.dtsi`） | ws.md の「I2C の AXP15060」は合っていた。どの I2C かは i2c5 |
| sound | 3.5 mm の 4 極の jack は「analog audio output」。mainline の DTS は **PWMDAC**（0x100b0000、`starfive,jh7110-pwmdac`、DMA は AXI DMA の 22 番）を `simple-audio-card` で jack に繋ぎ、codec は dummy（`linux,spdif-dit`）。外付けの codec の IC は DTS に無い | VF2 datasheet §5.7、DTS | ws.md の「codec」は無い（PWM の DAC）。PWMDAC の register は TRM に無い（§0.2） |
| HDMI | HDMI 2.0。TRM の map に DC8200（0x29400000 付近）・`U0_HDMITX`（0x29590000）・VOUT_CRG（0x295c0000）。mainline の v6.12 の DTS には voutcrg だけで display・HDMI の node が無い | TRM、DTS | p011 |
| 40 pin の UART | pin 8 = GPIO5（UART TX）、pin 10 = GPIO6（UART RX）、pin 6・9 = GND | VF2 datasheet §5.1 | — |
| boot の switch | RGPIO_1/0: 00 Flash（QSPI の NOR、既定で SPL と U-Boot が入る）、01 SD、10 eMMC、11 UART | VF2 datasheet、StarFive の README | 板の今の設定と flash の U-Boot の版（§6 #20） |
| 再起動 | `gpio-restart`（sysgpio 35） | DTS | 電源断は OpenSBI の SRST に任せる（未確認） |

### 1.2 DMA の coherency（design.md §1.6 を直す）

- design.md（改訂 2）の §1.6 は「JH7110 の周辺の DMA は CPU の cache を snoop しない。Linux は ccache の FLUSH64 で clean+invalidate する」と書いた。
- 2026-10-19 に確かめた事実: Linux v6.12 の `sifive_ccache.c` は FLUSH64 による非 coherent の cache の操作を **`starfive,jh7100-ccache` だけ**に登録する（QUIRK_NONSTANDARD_CACHE_OPS の表）。JH7110 の ccache（`starfive,jh7110-ccache`）には登録しない。JH7110 の `jh7110.dtsi` に `dma-noncoherent` は 1 つも無い（grep 0 件）。TRM の memory map は DDR の 0x0_4000_0000〜（memory port、属性 `C A N`）と 0x4_4000_0000〜（system port、属性 `N`）の 2 つの窓を書く。
- → **JH7110 の周辺の DMA は coherent（memory port を通る）と推定する**。design.md の記述は JH7100 の事実の取り違えの可能性が高い。実機で確かめるまで「推定」とする。
- 影響: 既存の driver（pci-xhci・pci-nvme・rpi4-ethernet の形）は `drv_dma_device_create` の `constraints.coherent` を 1 にでき、`drv_dma_alloc_coherent` は uncached の写像（VF2 では `hal_pmem_map_uncached` が `HAL_ERR_UNSUPPORTED`）を要らない。**coherent でなかった時**は非 coherent の device の coherent な確保が失敗する（`dma.c` の弱い参照の説明）ので、案 (i) `hal_pmem_map_uncached` を VF2 の BSP で system port の窓（物理 + 0x4_0000_0000、属性 N）への写像として実装する（HAL の実装の変更で API は不変。design.md の「VF2 では UNSUPPORTED」の決定を変えるので Q1 の確認が要る）、(ii) driver の ring を streaming の sync に書き換える（大きい）。既定は (i) を予備に持つ。
- 実機の確かめ（p013 の最初）: DMA を使う一番小さい device（SD の IDMAC か GbE の descriptor）で、coherent=1 のまま cache を flush せずに読み書きの一致を 1 MiB 程度で確かめる。食い違えば (i)。

### 1.3 割り込みの番号

- TRM の割り込みの表の `global_interrupts[n]` と DTS の `interrupts` は n + 5 の関係（UART0 27 → 32、PLDA PCIe0 51 → 56、SDIO0/1 69/70 → 74/75）。driver は DTS の値だけを使い、TRM の n を直に使わない。

### 1.4 U-Boot と framebuffer（p011 (1) の材料）

- StarFive の配布の U-Boot（`JH7110_VisionFive2_devel` の branch、SDK の版は 2021.10）の defconfig は `DM_VIDEO`・`VIDEO_STARFIVE`・`DISPLAY_STARFIVE_HDMI`・`VIDEO_COPY` を持ち、`drivers/video/starfive/` に `sf_hdmi.c`・`sf_vop.c` などがある（GitHub の API の一覧で確かめた）。
- mainline の U-Boot v2025.01 の defconfig には VIDEO の行が無く、`drivers/video/` に StarFive の driver も無い（`dw_hdmi.c` は他の SoC 用）。
- U-Boot の EFI は video の uclass の device があると GOP を出す（U-Boot の一般の挙動。VF2 で実際に出すかは**未確認**）。→ **配布の U-Boot なら GOP が出る見込み、mainline なら出ない**。どちらが板の flash に入っているかが要る（#20）。
- loader（p004）が GOP から ZBL6 の framebuffer を渡す道は amd64・arm64 と同じ。kernel は汎用の framebuffer の console（`hal-console.c` の形）で描ける。注意: GOP の framebuffer の memory は U-Boot の予約で、EFI の memory map で `EfiReservedMemoryType` か boot services の型になる。boot services の型なら kernel が上書きの対象にしない事を loader が保証する（p011 の確かめ）。HDMI の display の状態は ExitBootServices の後も U-Boot が止めない前提（U-Boot は ExitBootServices で `dm_remove_devices_flags` を呼び、video の device が remove で display を止める場合がある: **未確認**）。

## 2. 共通の設計

### 2.1 driver の置き場所と境界

- **HAL に置く物**: design.md のとおり ccache・CPU・timer・PLIC・SBI の console だけ。周辺の clock・reset・pinctrl・GPIO・I2C は HAL に入れない（[Guardrail](../guardrail.md)「HAL と kernel の受け渡し」: HAL → kernel は汎用の構造体と名前付きの handoff object だけ）。
- **driver の置き場所**: `src/drivers/platform/visionfive2/`（rpi4 の `src/drivers/platform/rpi4/` と同じ形。板・SoC に固有の物）と、IP が汎用の物は `src/drivers/<種類>/`（例 `src/drivers/mmc/dw-mmc.c`、`src/drivers/ethernet/dw-eqos.c`、`src/drivers/pci/pci-plda.c`、`src/drivers/ethernet/yt8531.c`、`src/drivers/i2c/dw-i2c-fdt.c`）。既存の `lpss-i2c.c` は同じ DesignWare の I2C の core で、core の部分を共有に切り出すかは p013 で決める（既定は切り出さず、FDT の版を別に書く: lpss-i2c の動作を変えない）。
- **kernel の platform**: `src/kern/platform/visionfive2.c`（p005 が作る）が device の init の順を持つ（rpi4.c の形）。p013〜p019 はその init の表に 1 行ずつ足す。

### 2.2 device tree の取り扱い

- DTB は U-Boot が渡す物を使う（決定 2）。HAL が handoff object で kernel に渡し（決定 11）、driver は `src/drivers/generic/fdt.c`（`drv_fdt`、rpi4 が使う）で node を `compatible` で探し、`reg`・`interrupts`・`clocks`・`resets`・`*-gpios`・`pinctrl-0` を読む。
- U-Boot の版で DTB が違う: 配布の U-Boot（2021.10）は配布の kernel（5.15）の DTB を渡し、mainline の U-Boot は mainline に近い DTB を渡す。compatible や clock の番号が違う可能性がある（**未確認**）。既定: **kernel は mainline の binding（v6.12 の DTS、§0.1）だけを支え**、DTB は zedBSD の image の ESP に置いた mainline の DTB を loader が U-Boot の物の代わりに渡す道を p005 で用意する（U-Boot の `fdt` の command か loader の option。p005 の範囲の確認を Q1 に頼む、#21）。
- clock と reset の番号は DTS の binding の header（clock-jh7110-crg.h・reset-jh7110-crg.h）の値を Value(s) referenced で使う。

### 2.3 試験の道（QEMU と実機）

- QEMU には JH7110 の machine が無い（`sifive_u` は FU540）。JH7110 の固有の block（CRG・pinctrl・DW MMC・PLDA・DW QoS・PWMDAC）は **実機だけ**。
- QEMU の virt で試せる物: PCIe の先の既存の driver（xHCI・usb-hid・NVMe）の riscv64 での動作。virt の PCIe は汎用の ECAM（`pci-host-ecam-generic`）なので、小さな FDT の ECAM の host の driver（p016a、新しい）を足せば、`-device qemu-xhci -device usb-kbd -device nvme` で riscv64 の xHCI・HID・NVMe・block の層を板の前に試せる。virt の DMA は coherent で、§1.2 の推定と同じ形。
- host の試験: clock の表（親・分周・gate の bit の計算）、DW MMC の command の組み立て、PLDA の ATR の窓の計算、YT8531 の拡張 register の値の計算を、host の C の短い試験にする（`plan/ws256/tests/`）。
- 実機の試験は T1/T2 が流す（AGENTS.md「試験の担当 T1」）。判定の方法は §5（#15）。

## 3. Phase ごとの設計

各 Phase の表: **既存** = そのまま使う既存の code、**新規** = 書く物、**HAL** = HAL の API の変更、**DT** = device tree の読み方、**試験**、**依存**、**LW**（1 LW ≈ 15〜25 分、±50%）。

### 3.1 p011 framebuffer（(1) 調べ）

- **(1) 調べ**（実機、p005 の UART の login の後）: 板の flash の U-Boot の版を UART の banner で読み、HDMI に monitor を繋いで U-Boot が画を出すか、`bdinfo` の FB の行と EFI の GOP（loader の log か `efidebug`、U-Boot に command があれば）を読む。GOP が出れば loader（p004）の GOP → ZBL6 の framebuffer の道をそのまま使い、kernel は汎用の framebuffer の console（arm64・amd64 と同じ）。ExitBootServices の後も画が残るかを見る（§1.4 の未確認）。
- 出ない（mainline の U-Boot）時の選択肢: (a) 配布の U-Boot を SD か flash に入れて使う（板の flash の書き換えはユーザーの判断、#20）、(b) 自前の display の driver（DC8200・HDMI TX・VOUT の PMU・voutcrg。TRM は map と clock だけで、DC8200・HDMI TX の register は無い → 出典は配布の U-Boot の `sf_vop.c`・`sf_hdmi.c` の値（GPL、値の引用）か vendor の Linux。大きい: 未見積、design-reviewer の後に Phase に分ける）。既定は (a)。
- **既存**: loader の GOP の取り込み（`bootloader/uefi/`、p004 の riscv64 版）、`src/kern/platform/hal-console.c` の framebuffer の console。**新規**: (1) は code 無し、(2) は別の設計。**HAL**: 無し（framebuffer は ZBL6 の汎用の handoff）。**DT**: 使わない（GOP の情報だけ）。U-Boot が `simple-framebuffer` の node を DTB に足す場合はその reg と照合して log に出す。
- **試験**: 実機だけ。画面は HDMI の capture（§5 #15 の (B)）か、ユーザーが目で見る。**依存**: p004・p005。**LW**: (1) 1〜2。

### 3.2 p013 周辺の基盤（clock・reset・pinctrl/GPIO・PMIC）

- **方針**: U-Boot が残した状態に頼らず、各 driver が要る clock の gate と reset の解除を自分で行う（冪等）。pinmux は U-Boot が SD・UART・GbE・PCIe の pin を設定済みの見込みなので、最初は**読んで期待の値と照合して log に出すだけ**にし、違えば設定する（SD の card detect の GPIO 41、PCIe の PERST の 26・28 の出力）。
- **新規** `src/drivers/platform/visionfive2/`:
  - `vf2-crg.c`: SYS CRG（0x13020000）・STG CRG（0x10230000）・AON CRG（0x17000000）の gate（bit 31）・分周・mux と reset（各 CRG の reset の assert の register と status の register）。API は「DT の (CRG の phandle, 番号) → enable・disable・set_rate・get_rate」と「(phandle, reset の番号) → assert・deassert」。表は TRM §2.8.1〜2.8.3 の register 表と binding の番号から作る（OpenBSD の stfclock.c の手順を参考）。範囲は p014〜p019 が使う clock だけ（SDIO1・PCIe0/1・STG の AXI・GMAC0/1・PWMDAC・I2C5・AXI DMA）。set_rate は GMAC の TX（125/25/2.5 MHz）と SDCARD だけ。
  - `vf2-pinctrl.c`: SYS IOMUX（0x13040000）と AON IOMUX（0x17020000）の GPIO の出力・入力・方向と、GPO/GPI の mux（TRM §2.7・§2.8.7、jh7110-pinfunc.h の番号）。GPIO の割り込みは使わない（SD の card detect は poll）。
  - `vf2-syscon.c`（小さい）: STG SYSCON（PCIe の AXI の設定）・SYS SYSCON（SDIO の `starfive,sysreg`）の read-modify-write。
  - **PMIC（AXP15060）は p013 の範囲から外す（既定）**: SD・PCIe・GbE・sound の電源は DTS で always-on か PMIC 以外（§1.1）で、PMIC の操作は eMMC の 1.8 V と CPU の電圧だけ。電源断は OpenSBI の SRST、再起動は gpio-restart。I2C の DW の FDT 版と AXP15060 の read（ID の読み出し）は Future Work か、要る時に足す（Q1 の確認、#22）。
- **既存**: `drv_fdt`（`src/drivers/generic/fdt.c`）、rpi4-gpio.c の書き方。**HAL**: 無し。**DT**: `clocks`・`resets`・`assigned-clock-rates`・`*-gpios`。
- **試験**: host の試験（表の計算）。実機: 各 CRG の id の register を読む、GPIO 41 の card detect を挿抜で読む（log ではなく、shell の command の答えで: §5）、§1.2 の coherency の確かめは p014 の IDMAC と組で。
- **依存**: p005（VF2 の platform）、p012。**LW**: 6〜9。

### 3.3 p014 SD カード

- **新規** `src/drivers/mmc/dw-mmc.c`（Synopsys DesignWare の Mobile Storage Host、IDMAC の内部 DMA、64 bit の descriptor）と `src/drivers/platform/visionfive2/vf2-mmc.c`（JH7110 の glue: biu/ciu の clock、reset、`starfive,sysreg` の sample の phase）。SD の card の初期化の手順（CMD0・8・ACMD41・2・3・9・7・ACMD6・CMD16、high speed の CMD6）は既存の `rpi4-sdhci.c` にあるので、**SD の protocol の層を rpi4-sdhci.c から共有の file（例 `src/drivers/mmc/sd-card.c`）に切り出すか**を p014 の最初に決める（既定: 切り出す。rpi4 の動作を変えない事を rpi4 の build と host の試験で確かめ、実機の Pi の確認は T1 に依頼）。block の device として既存の MBR・GPT・FAT の層（`src/drivers/disklabel/`）に繋ぐ。
- 速度: 50 MHz の 4 bit（25 MB/s 前後）まで。UHS は無し（§1.1）。
- **HAL**: 無し。**DT**: `starfive,jh7110-mmc` の `mmc1`（`mmc0` は eMMC で無視）、`fifo-depth`、`cd-gpios`、`bus-width`、`max-frequency`。
- **試験**: QEMU には DW MMC が無い → 実機だけ。host の試験（descriptor の組み立て）。実機: card の識別、raw の 64 MiB の読み書きの一致、MBR の partition の mount（p015 の前に ESP の FAT を ls）。
- **依存**: p013。**LW**: 8〜12（sd-card の切り出しを含む）。

### 3.4 p015 rootfs からの起動

- SD の image（ESP・root、p005 の手順）で、kernel が root を SD の root partition から mount し、initrd 無しで init まで行く。画面は p011 の framebuffer、無ければ UART。
- **既存**: rpi4 の SD の root の道（`src/kern/platform/rpi4.c` の root の選び方と `vfs.c`）。**新規**: visionfive2.c の root の選択（command line の `root=` か既定の SD）。image の build の手順（`plan/ws256/tests/` の config.mk、AGENTS.md の試験の image の作り方）。**HAL**: 無し。**DT**: 無し。
- **試験**: 実機で 10 回の起動と login（判定は §5）。**依存**: p011（画面で login する場合）・p014。**LW**: 3〜5。

### 3.5 p016 PCIe の host と USB・keyboard

- **p016a（QEMU、板の前に可）**: `src/drivers/pci/pci-ecam-fdt.c`（`pci-host-ecam-generic` の FDT の host。config は ECAM、BAR は DT の `ranges` から割り当て、INTx は `interrupt-map` から PLIC の番号）。これで virt で既存の pci-xhci・usb-hid・pci-nvme を riscv64 で動かす。pci-brcmstb.c の host の形（`describe`・`start`・`publish`、`host_config_read/write`・`host_map_bar`・`host_allocate_irqs`）をそのまま使う。MSI は virt の AIA が無いので INTx だけ（既存の xHCI・NVMe が INTx で動く事を確かめる: **未確認**、MSI 必須なら MSI の無い時の道を足す）。
- **p016b（実機）**: `src/drivers/pci/pci-plda.c`（PLDA の PCIe の root port: apb の register、ATR の outbound（cfg・32 bit・64 bit の窓）と inbound（DDR）の窓、link の train、PERST の GPIO、PHY（stfpciephy は小さい: PCIe の mode の設定）、MSI の controller の中の demux、INTx の中の intc）。STG SYSCON の PCIe の AXI の設定と STG CRG の clock・reset は p013 の API。OpenBSD の stfpcie.c・stfpciephy.c の手順を参考、ATR の offset などの値は Value(s) referenced。
- USB: 既存の `pci-xhci.c`・`usb-hub.c`・`usb-hid.c`・`hid-input.c` をそのまま使う。VL805 なら Pi 4 と違い VideoCore の firmware の load は無い（板の SPI EEPROM の見込み: **未確認**）。rpi4-pcie.c の VL805 の firmware の通知の道は使わない。
- keyboard の入力を console に繋ぐ: 既存の input-bridge の道（arm64・amd64 と同じ）。
- **HAL**: 無し（MSI は host の driver の中の demux で、PLIC の 1 本の線）。**DT**: `starfive,jh7110-pcie`（reg の cfg・apb、ranges、interrupts、interrupt-map、`perst-gpios`、`phys`、`starfive,stg-syscon`、clocks・resets）、`pci-host-ecam-generic`（virt）。
- **試験**: p016a は QEMU の virt（T1、boot-test.sh の画面: keyboard の打鍵は QMP の `send-key` で、画面の答えで判定）。p016b は実機: link up、`pci` の列挙の答え、USB keyboard で login。
- **依存**: p016a は p004（virt の起動の道）と p007、p016b は p013・p016a。**LW**: p016a 4〜6、p016b 8〜12。

### 3.6 p017 NVMe

- **既存**: `pci-nvme.c`（変えない見込み）と block の層。**新規**: 無し（NVMe の root の選択は p015 の root の選び方に 1 項目）。
- **試験**: QEMU の virt（`-device nvme`、p016a の上）で読み書きと mount。実機: 付いている M.2 の SSD の識別・読み書きの一致・NVMe の root で起動（ユーザー「NVMe SSDがボードにつけてあり、テスト可能です」）。
- **依存**: p016（virt の試験は p016a だけ）。**LW**: 2〜4（実機の debug は別）。

### 3.7 p018 GbE と PHY

- **新規** `src/drivers/ethernet/dw-eqos.c`（Synopsys の Ethernet QoS 5.20: DMA の descriptor の ring、MTL、MAC、MDIO）、`src/drivers/platform/visionfive2/vf2-ethernet.c`（JH7110 の glue: AHB・AXI・PTP・TX・RX の clock、TX の clock の速度ごとの set_rate、RGMII の mode の SYSCON の bit、reset）、`src/drivers/ethernet/yt8531.c`（Motorcomm の PHY: 拡張 register の RGMII の delay と DTS の `motorcomm,*` の駆動の値。bcm54213pe.c と同じ形）。network の stack への繋ぎは rpi4-ethernet.c と同じ口。
- 版の差: v1.3B は両方 RGMII、v1.2A の gmac1 は RMII → 既定は **gmac0 だけ**を支える（v1.2A でも gmac0 は RGMII で delay の値だけが違う、DTS で読める）。
- **HAL**: 無し。**DT**: `starfive,jh7110-dwmac`、`phy-handle`・`phy-mode`・`mdio`、`rx/tx-internal-delay-ps`、`motorcomm,*`、`starfive,tx-use-rgmii-clk`、`assigned-clock-parents`。
- **試験**: 実機だけ: link up、DHCP、host から ssh で login（`plan/tools/guest/guest.sh` の形。§5 の (C)）。
- **依存**: p013。**LW**: 10〜14。

### 3.8 p019 sound

- **道の決定（既定、#23）: 3.5 mm の jack の PWMDAC**。HDMI の audio は p011 の (2)（自前の display）が要り、配布の U-Boot の GOP の道では kernel が HDMI を持たないので使えない。
- **新規** `src/drivers/platform/visionfive2/vf2-pwmdac.c`（PWMDAC: 16 bit の stereo の FIFO を DMA か割り込みで埋める）。DMA は SoC の AXI DMA（`starfive,jh7110-axi-dma`、0x16050000、DW の AXI DMAC）の 22 番の channel → `src/drivers/dma/dw-axi-dmac.c`（新規、最小の cyclic の転送）が要る。**DMA 無しの PIO で埋められるか**（FIFO の深さと割り込みの有無）は PWMDAC の register の資料が TRM に無いので**未確認**。既定: 最初は DMA で設計し、p019 の最初に Linux の driver の値で FIFO と割り込みを読み、PIO で足りれば DMA の driver を省く。
- 既存の `src/drivers/audio/audio.c`（`drv_audio_register`）に繋ぐ。sample rate は PWMDAC の core の clock の分周で決まる（rpi4-audio と同じく 48 kHz を既定）。
- **HAL**: 無し。**DT**: `starfive,jh7110-pwmdac`、`dmas`、clocks・resets、`pinctrl-0`（pin は pinmux の PWMDAC_LEFT/RIGHT_OUTPUT、GPO 28・29）。
- **試験**: 実機だけ。判定は音が出た事（ユーザーの耳か、host の line-in の録音）→ #24。
- **依存**: p013。**LW**: 5〜8（DMA の driver を含めると上限）。

## 4. 依存と並行の組（P1・P2）

```
p004 → p005 ─┬→ p011(1)
             └→ p013 ─┬→ p014 → p015 (← p011)
                      ├→ p016b → p017(実機)
                      ├→ p018
                      └→ p019
p004・p007 → p016a → p017(virt)
```

並べる順の案（P2 は今 p008・p007・p004 を進めている）:

| 段 | P1 | P2 | 理由 |
| --- | --- | --- | --- |
| 1（板の前） | p016a（virt の ECAM と xHCI・HID・NVMe、p004 の merge の後）→ p017 の virt の部分 | p004 → p005 | p016a は QEMU だけで進む。p005 の実機の待ちと重ならない |
| 2 | p013 の code（板が来る前に書ける。実機の確かめは p005 の後） | p005 の実機の debug、p011 (1)（実機で 1〜2 LW） | p013 は全部の前提で、早く書いておく |
| 3 | p016b（PLDA）→ p017 の実機 | p014（DW MMC）→ p015 | 互いに触る file が重ならない（drivers/pci と drivers/mmc）。共有するのは visionfive2.c の init の表の 1 行ずつだけ |
| 4 | p018（GbE） | p019（sound） | 同上（drivers/ethernet と drivers/audio・dma） |
| 最後 | p006（規約、Q1 の割当） | | |

- 実機の試験は板が 1 枚なので、T1/T2 が順に流す。P1・P2 の実機の依頼が重なれば T1 がまとめる。
- 合計（p012 の見積もり）: p011(1) 1〜2、p013 6〜9、p014 8〜12、p015 3〜5、p016a 4〜6、p016b 8〜12、p017 2〜4、p018 10〜14、p019 5〜8 → **47〜72 LW**（実機の debug は別に p013・p016b・p018 で各 4〜8 を見込む）。p011 (2)（自前の display）は含まない。

## 5. 実機の判定の方法（#15）と接続（#10）: Q1 がユーザーに聞く選択肢

### #15 実機の判定（design.md §5.3 の案を、p011・p018 で増える道を足して並べ直す）

| 案 | 内容 | 使える時期 | 利点・欠点 |
| --- | --- | --- | --- |
| (A) serial の対話 | `plan/tools/guest/serial.py` に実機の tty を足し、`login:` を待って `root`・`uname -m` などを打ち、**答えの文字列**で判定する（log を読むのではなく対話）。WS256 の実機に限る例外 | p005 から | 画面が無くても判定できる。新しい用法なのでユーザーの明示の許可が要る |
| (B) HDMI の capture | host に USB の HDMI capture（UVC）を繋ぎ、`boot-test.sh` と同じく画面を撮って login の prompt を読む | p011 の GOP が出た後 | zedBSD の「画面で判定」に一番近い。capture の機器が要る（ユーザーの用意） |
| (C) SSH | p018 の後、host から `guest.sh` で SSH して判定（Linux guest の例外と同じ形） | p018 の後 | 速い。p018 より前には使えない |

**既定の案**: p005〜p014 は (A)、p011 で画が出れば (B) に移す（機器があれば）、p018 の後は (C) を足す。QEMU の virt は今までどおり画面（決定 5）。

### #10 実機の接続

| 項目 | 選択肢 | 既定の案 |
| --- | --- | --- |
| console | (a) 3.3 V の USB-UART を 40 pin の 6・8・10 番（GND・TX・RX）に、115200（b) なし | (a)。host の `/dev/ttyUSB*` の名前を固定（udev の by-id） |
| 電源 | (a) Pi と同じ SwitchBot の plug（`plan/tools/switchbot.py cycle`、Q1 が入れ直す）、(b) USB-C の PD の電源を手で | (a) |
| 起動の媒体の更新 | (a) SD を host で書いて差し替える（ユーザーの手）、(b) SD の mux（SDWire などの機器）、(c) U-Boot の TFTP（`dhcp`/`tftpboot` で BOOTRISCV64.EFI と vmunix を落として `bootefi`、U-Boot は DWC_ETH_QOS を持つ）、(d) NVMe に置く | 開発の繰り返しは (c)（SD の差し替えを減らす）、受け入れは (a) の SD の image（p015）と (d) の NVMe（p017） |
| LAN | 板の eth0 を host と同じ試験の network に。DHCP の address を固定 | p018 と (c) に要る |
| HDMI | monitor か capture（#15 (B)） | p011 の時に |

## 6. ユーザーに聞く判断（Q1 経由）と既定の案

| # | 問い | 既定の案 |
| --- | --- | --- |
| 15 | 実機の判定の方法 | §5: (A)→(B)→(C) |
| 10 | 実機の接続 | §5 の表 |
| 2 | 板の版（v1.2A・v1.3B）と memory | p018 の PHY の値と gmac1 の可否に効く。既定は gmac0 だけ |
| 20 | 板の flash の U-Boot の版、書き換えの可否 | 配布の U-Boot（HDMI の GOP が出る見込み）を使う。mainline なら p011 は (a)（配布の U-Boot を SD から）を試す。flash を書き換えるかはユーザー |
| 21 | DTB の出どころ | mainline の DTB を zedBSD の image に置いて使う（p005 の範囲に入るか Q1 が確認） |
| 22 | PMIC（AXP15060・I2C）を p013 から外すか | 外す（要るのは eMMC・CPU の電圧だけ） |
| 23 | sound の道 | 3.5 mm の PWMDAC（HDMI の audio は自前の display の後） |
| 24 | sound の判定 | ユーザーが耳で聞く、または host の line-in で録音して波形を確かめる。既定はユーザーの耳（試験の一覧に載せる、AGENTS.md「試験を頼まず一覧に」） |
| 25 | §1.2 の coherency の推定の採否 | 推定（coherent）で設計し、p013 の実機で確かめる。食い違えば `hal_pmem_map_uncached` を system port の窓で実装（design.md の「UNSUPPORTED」を変える） |
| 26 | p016 を p016a（QEMU）と p016b（実機）に分けるか | 分ける（板の前に P1 が進められる） |

## 7. 未確認（まとめ）

- VL805 の型番と、USB と M.2 がどちらの PCIe に付くか（回路図を取得していない。p016 で U-Boot の `pci` を読む）。
- JH7110 の DMA の coherency（§1.2、推定は coherent）。
- U-Boot が JH7110 で GOP を出すか、ExitBootServices の後も画が残るか（§1.4）。
- 板の flash の U-Boot の版、板の版、memory の大きさ。
- YT8531 の PHY の id（OpenBSD の ytphy.c は YT8521 だけ）と v1.2A の PHY。
- PWMDAC の register（FIFO の深さ・割り込み）と PIO で足りるか。
- 既存の pci-xhci・pci-nvme が INTx だけで動くか（virt の p016a）。
- 配布の U-Boot の DTB と mainline の binding の差。
- 電源断が OpenSBI の SRST で効くか。
