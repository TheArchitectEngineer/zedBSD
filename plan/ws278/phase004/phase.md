<!-- awesome-plan project=zedbsd record=ws278-p004 -->

# ws278-p004 RP1 の USB（DWC3 の xHCI、poll）

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i02（P3、2026-10-21）
- Status: in-progress（実装と host の確認まで。実機の確認は WS278 の最後の一括の試験）

## 目的と受け入れの条件

Pi 5 の USB の 4 つの口（RP1 の 2 つの DWC3、各々 xHCI）で USB keyboard・hub・USB storage を使う。

- build（warning 0、rpi5）、host の試験 `rpi5-rp1-host-test`（C1・D0 の dtb）が PASS。
- 実機（一括の試験）: `rp1: bus 1, peripherals at PCI 0 …`、`rp1-usb: DWC3 … in host mode` ×2、`xhci: PCI controller, version=… ports=…` ×2、USB keyboard で login、USB storage の `dmesg`。

## 設計

- `config.txt` の `[pi5]` に `pciex4_reset=0`。firmware は PCIe x4 の link と RP1 の BAR（BAR0 = MSI-X の表、BAR1 = 周辺の窓、PCI 0 番地）をそのまま残す。
- `src/drivers/platform/rpi5/rp1.c`（RP1 の module）: device tree の `brcm,bcm2712-pcie` の子の `rp1`（simple-bus かつ interrupt-controller）を探し、root complex の register を写像し、link（PHYLINKUP・DL_ACTIVE）、root port の secondary bus、RP1 の ID（0x1de4:0x0001）、2 つの BAR を読む。root complex の inbound の窓（RC_BAR1〜3 と UBUS の remap）を pcie の dma-ranges から設定し直す（RP1 の DMA と、p004b の MSI の書き込みの通り道。firmware と違えば log）。rp1 の dma-ranges から周辺の DMA の窓を読む（最初の「CPU の 0 に着く」項: device の番地 = 物理 + 0x10_0000_0000、Linux と同じ選び方）。RP1 の command に MEM・MASTER。子の reg を CPU の番地に直す（rp1 の ranges → BAR1 → root complex の ranges）。
- `rp1-gpio.c`: RP1 の GPIO（3 つの bank、control・RIO・pads の 3 つの block）。function の選択と GPIO の出力。USB の VBUS の pin（usb@200000 の pinctrl-0 = GPIO 42/43 の vbus1、selector 2）を繋ぐ。p005 の PHY の reset（GPIO 32）にも使う。
- `rp1-usb.c`: 各 DWC3 を host にする（GCTL の PRTCAPDIR = host、GUSB2PHYCFG0 の USBTRDTIM = 9・PHYIF = 0、OpenBSD の xhci_snps_init と同じ手順）。controller を PCI core に「この module だけの root bus」の function として見せる: 設定空間は memory に持ち（ID 0x1de4:0x0001、class 0x0c0330、64 bit の BAR0 = controller の register の PCI 番地・大きさ 1 MiB、INTA）、BAR の sizing の probe に正しく答える。既存の xHCI の driver（`pci-xhci.c`）が class で bind する。DMA の device は RP1 の窓（offset 0x10_0000_0000、64 bit、非 coherent）。
- 割り込み: host 自身の pin（`intx_establish` 等の 4 つの hook）。p004 は kthread が 2 ms ごとに PCI core の line を割り込みを止めて呼ぶ（poll）。p004b で RP1 の MSI-X → MIP → GIC の本当の割り込みに替える。
- USB の driver の登録（HID・hub・storage・UAS・xHCI）は rpi4-pcie.c の一覧を写して rp1-usb.c に（ユーザーの「共通化せず写す」）。`config-rpi5.mk` で XHCI・USB_HID・USB_HUB・USB_STORAGE を y。
- 出典: OpenBSD の bcm2711_pcie.c（PCIe の register、inbound の窓の手順）・rpone.c（RP1 の BAR）・xhci_fdt.c（DWC3 の手順）・pcidevs、FreeBSD の dwc3.h（GSNPSID）、Raspberry Pi の Linux の pinctrl-rp1.c（GPIO の register と function の表、GPL の値の引用）。各 block の「Value(s) referenced / Procedure referenced」。

## 変えた所

- 新: `src/drivers/platform/rpi5/rp1.c/h`（前の generation の途中の物を直して足した）、`rp1-gpio.c/h`、`rp1-usb.c/h`、`tests/rpi5-rp1-host-test.c`。
- `src/kern/platform/rpi5.c`（`rpi5.fdt` を読み RP1 と USB を始める、refresh、HID の input）、`platform/arm64/vmunix.mk`（rpi5 の source と host 試験の規則だけ）、`config/ci/config-rpi5.mk`、`platform/arm64/config-rpi5.txt`。rpi4 の build に入る物は変えていない。

## 確認（2026-10-21 P3、host）

- `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk BUILD=build/rpi5 build/rpi5/kernel.elf -j16`: rc=0、warning 0、`arm64 vmunix check: PASS`。
- `make … rpi5-rp1-host-test`: C1・D0 とも PASS（USB0 0x1f_0020_0000・USB1 0x1f_0030_0000・GEM 0x1f_0010_0000、DMA offset 0x10_0000_0000・limit 0xf_ffff_ffff・非 coherent、inbound の窓 3 つの値、GPIO 42 の selector と pad）。
- 未実施: 実機（Pi 5）。rp1-usb.c の設定空間の emulation は host で試していない（PCI core と xHCI に繋がるので、実機で見る）。

## 危険

- R7: firmware が `pciex4_reset=0` でも RP1 の link や BAR を残さない版なら `rp1: PCIe link down …` で USB 無しの起動（止まりはしない）。
- R8: inbound の窓の設定し直しで firmware の設定を壊す可能性。log の `rp1: inbound window N was …` で firmware の値が分かる。
- R9: SD から起動した時に bootloader が USB を触っていなければ VBUS の pin の mux は kernel が付ける（GPIO 42/43）。keyboard に電源が来なければ、ここを疑う。

## 1 回目の実機の結果と対処（2026-10-22 P3）

- 実機: USB・LAN とも使えない（ユーザー）。写真では前半が流れて `rp1`・`rp1-usb`・`xhci`・`gem` の行が見えない。
- 黙って失敗し得る所（code から）: device tree が無い（`start_rp1` が何も言わずに戻っていた）、firmware が `pciex4_reset=0` でも link を落とす（`rp1: PCIe link down`）、RP1 の BAR が無い、inbound の窓の設定し直し（`rp1: inbound window N was …`）、DWC3 の identity が読めない（`start_core` が何も言わずに ENODEV）、xHCI の bind・enable slot の completion が来ない（MSI-X → MIP の経路か DMA の窓）、RP1 の割り込みの初期化の失敗。
- 対処: 段ごとの 1 行を足した。`rpi5: no device tree; …`、`rp1: PCIe link up (status …), bus … device 0 is …`、`rp1: bus …, MSI-X table at PCI …, peripherals at …`、`rp1-usb: no DWC3 at … (identity …)`、`rp1-usb: no controllers to start`。pci-xhci.c の Pi 4 の enable slot の追跡の行を Pi 5 でも出す。全部の log は bootlog の file で読む（p002）。

## 2 回目の実機の結果と対処（2026-10-22 P3）

- 実機（ユーザー「まだだめですね」、log `build/review/rpi5/uat-20261022-2-boot-000001.txt`）: `rp1: the root port has no bus behind it` → `rpi5: RP1 unavailable (13)` → `rp1-usb: no controllers to start`。link（PHYLINKUP・DL_ACTIVE）は上がっていた（`rp1: PCIe link down` は出ない）ので、config.txt の `pciex4_reset=0`（image の FAT の config.txt にある）は効いている。
- 原因: firmware は link を残すが、root port（root complex 自身の設定空間）の secondary bus 番号を 0 のまま渡す。zedBSD は firmware が bus を番号付けした前提で、0 なら諦めていた。Linux（pcie-brcmstb.c）は root bus の設定空間を root complex の register に、その下の bus を EXT_CFG_INDEX の bus 番号で引き、bus 番号は自分の列挙で付ける（firmware の値に頼らない）。
- 対処（`rp1.c`）: 列挙と同じく、root port が bridge（header type 1）なら primary 0・secondary/subordinate 1 を付け、memory の窓を 32 bit 全体に、prefetch の窓を閉じ、command に MEM・MASTER。MISC_CTRL の CFG_READ_UR_MODE を立て、答えの無い設定空間の読みを abort でなく all ones に（Linux と同じ）。outbound の窓 0 が空なら pcie の ranges から設定（Linux の brcm_pcie_set_outbound_win と同じ形）。RP1 の BAR0 と BAR1 が同じ番地（未配置）なら、size を測り BAR1 を rp1 の ranges の PCI 番地に、残りを後ろに置く。firmware の値を `rp1: root port: …`・`rp1: outbound window: …` の 2 行で log に残す。
- host 試験 `rpi5-rp1-host-test`（偽の root complex を UAT と同じ「bus 番号 0・header type 1」にした）: C1・D0 とも PASS（bus 番号 0x00010100、outbound の窓 0x4070=fff00000・0x4080/0x4084=0x1f）。
- 未実施: 実機（3 回目の試験）。
