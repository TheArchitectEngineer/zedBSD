<!-- awesome-plan project=zedbsd record=ws256-p020 -->

# ws256-p020 PLDA の PCIe の host

Status: in-progress（板の前の code は済み、2026-10-19 17:04。実機の確かめ（p022・p023 の後、ユーザーが framebuffer を目で見る）と Q1 の判定が残り）

- 親: [WS256](../ws.md)。Queue: q1291-i01（P1、板の前の code）。
- 範囲: [design-peripherals.md](../design-peripherals.md) §3.5 の p020（改訂 4、Minor 9・11・12）。`pci-plda.c`（apb の register、ATR の outbound（cfg・32 bit・64 bit）、PHY の値、link の train、PERST、p021 の hook での INTx の demux、swizzle）、visionfive2.c からの 2 つの host の attach、pci-xhci・usb-hid・usb-hub・pci-nvme への繋ぎ。HAL（hal.h）は変えない。
- 完了の条件: host 試験が合格、build warning 0（VF2・virt）、実機で link up・列挙・USB keyboard で login・NVMe の読み書き（下の「実機で見る行」）、Q1 が合格とする。

## 実装（2026-10-19、P1、worktree /home/awe/zedBSD-worktrees/p1、main 1cf93acba の上）

| file | 中身 |
| --- | --- |
| `include/drivers/pci/pci-plda.h`・`src/drivers/pci/pci-plda.c` | 汎用の PLDA の host。`describe`（reg-names の cfg・apb、ranges の memory の窓（最大 4）、interrupts、`linux,pci-domain` を segment に、interrupt-map の自分の intc の hwirq）、`start`（IMASK 0・ISTATUS 全部 W1C → ATR 0 = cfg（config 要求）・ATR 1.. = 各 memory の窓 → firmware が残した endpoint の状態を log → PERST assert → root port の追加の function 3 つを無効 → root port・bridge の class・64 bit prefetch・LTR を外す → 100 ms → PERST 解除 → link を 1 ms × 100 回待つ → 100 ms 待つ → bus 1 の device 0 の BAR を 32 bit の窓の先頭から置き、root port の memory の窓と MEM・MASTER → 共有の PLIC の線を `kern_irq_register`、ISTATUS を W1C して unmask）、`publish`（DMA は 32 bit・coherent は引数、root bus 0 を作って scan）。INTx は p021 の 4 つの hook（irq-save の host の lock、pin ごとの `in_flight`、disestablish は 1 ms × 1000 回の drain、超えたら元に戻して EBUSY）。ISR は status を読み、`status & 許可した pin`（vector v ↔ bit 24 + (v − 1)）だけを lock を外して `run`、全部の後に読んだ status を W1C、EOI を 1 回。swizzle は bridge ごとに slot で回す（pci-ecam-fdt.c と同じ形）。MSI・event は IMASK に立てない |
| `src/drivers/platform/visionfive2/vf2-pcie.c/.h` | 板の側: `starfive,jh7110-pcie` を 2 つまで、cfg の address で STG の group（0x048・0x1f8）、pinctrl の default、STG の root port（no-endpoint）・reference clock 2・clock request、clock 4 つ（noc・tl・axi_mst0・apb）、reset 6 つ（mst0〜apb）、PHY の 2 つの register（0x28 = 0x91、0x80 = 0x0c）、`perst-gpios`。board の op: PERST（最初に output にする）、link（STG 0x170 bit 5）、function の選択（STG 0x078・0x07c の field）。xHCI・usb-hid・usb-hub・usb-storage（config で y の時）・NVMe の driver の登録、refresh・input_ready |
| `src/kern/platform/visionfive2.c` | `probe_soc` の最後に `drv_vf2_pcie_init(&fdt, VF2_DMA_DEFAULT_COHERENT)`、`kern_platform_refresh_devices` に refresh、`kern_platform_input_init` に input_ready（3 か所と include だけ。P2 の p014 とのぶつかりを小さくした） |
| `platform/riscv64/vmunix.mk` | PCI の先の driver の source を `RISCV64_PCI_DEVICE_SOURCES` に分け、virt と visionfive2 の両方で使う。visionfive2 は vf2-pcie.c・pci.c・pci-plda.c |
| `config/ci/config-visionfive2.mk` | `CONFIG_DRIVER_PCI_XHCI`・`_PCI_NVME`・`_USB_HID`・`_USB_HUB` を y |

- 出典: register の offset・bit・ATR の形・STG の offset・PHY の値は OpenBSD stfpcie.c・stfpciephy.c 1.4（ISC）を `Value(s) referenced:`、手順も同じく `Procedure referenced:`（block ごと、file の先頭には書かない）。PCIe の時間は PCIe Base Spec 4.0 §6.6.1、xHCI・NVMe の register は各仕様。GPL（Linux）の PLDA の手順は使っていない（§0.2 の表どおり）。
- **決めた事（design からの判断）**:
  1. **inbound の ATR は読まない**。design は「PCIe → AXI の ATR を読んで答えに出す」だが、inbound の register の offset は TRM にも OpenBSD にも無く、出典が Linux（GPL、PLDA は「手順の参考を許す」の一覧の外）しか無い。値を推測で読むのはやめ、root の DMA は design の既定の 32 bit のまま（OpenBSD も inbound を書かずに動く）。64 bit に広げるのは出典が得られた時（Q1 への報告）。
  2. **root port の BAR0・BAR1 は read 0・write 無視にした**。core は bridge の BAR も all-ones を書いて測る（pci.c の read_device）。root port の BAR は誰も使わず、測る書き込みが block の decode を一時的に変えうるので、自分の判断で隠した（出典無し、Linux の手順は見ていない）。
  3. OpenBSD の mmio の ATR の loop は全部の entry に `sc_ranges[0]` を書く（誤りと読める）。zedBSD は entry ごとに自分の窓を書く（host 試験で ATR 2 が 64 bit の窓になる事を確かめた）。
  4. link up の後に 100 ms 待ってから config を読む（PCIe Base Spec §6.6.1、OpenBSD には無い）。
  5. firmware の状態の読み取り（Minor 12）は ATR を書き直した後、PERST の前に、bus 1 の device 0 を読んで行う（root port の bus 番号を先に 1 にする）。xHCI なら BAR0 の CAPLENGTH の先の USBSTS、NVMe なら CC・CSTS。BAR が窓の外・MEM が off なら読まない。
  6. 2 つの host は PCI core の segment 0・1（`linux,pci-domain`）。bus 0 は root port だけ、bus 1 は device 0 だけ、その下の bus は無い（VF2 の板は root port の直下に endpoint 1 つ）。

## host の試験（2026-10-19）

`plan/ws256/tests/pci-plda/run.sh OUTDIR`（本物の pci-plda.c・pci.c・fdt.c を fake の register の上で、通常と ASan/UBSan の 2 回。`vf2-pcie.dts` は JH7110 の DT の 2 つの host と disabled の 1 つ）。rm はしない。

- 結果: `plan/ws256/tests/pci-plda/run.sh build/tmp/p020-host` → `pci plda: 137 PASS, 0 FAIL` ×2、`ordinary, ASan/UBSan PASS`。
- 中身: describe（cfg・apb を名前で、irq 56・57、segment、2 つの窓、pin n → vector n、disabled は ENODEV）。ATR の値（host 0: entry 0 = 0x40000031・high 9・kind 1、entry 1 = 0x30000037・target 0x30000000・kind 0、entry 2 = 0x0000003d・high 9・target 0x9_0000_0000、entry 3 は空。host 1 は 0xc0000031・0x38000037・0x8000003d）。root port の設定（GEN bit 0、class 0x06040000、BAR_WIN bit 3、LTR だけ clear、追加の function 1・2・3 を選んだ間に 3 回書いて 0 に戻す）。PERST 1 回 assert・解除、link up、bus 番号 0x00010100、endpoint の BAR が窓の先頭、root port の窓、MEM・MASTER、root port の BAR に書き込み 0 回。firmware の状態の log（`firmware left 1106:3483 class 0c0330`、`HCHalted 1`、`NVMe CC.EN 1, CSTS.RDY 1`）。DMA 32 bit・coherent。割り込み: 2 つの host の同じ hwirq 1 で別の handler、INTA+event で handler 1 回・W1C は handler の後・EOI 1 回で token が戻る、許可していない INTB は handler 無しで W1C と EOI、status 0・event だけも EOI 1 回、handler の中で host の lock が空、host 0 の解除が host 0 の IMASK だけを 0 にし host 1 は残る。
- 変異の確かめ（build/tmp/p020-mut、写しを変えた）: ATR の loop を OpenBSD と同じ `windows[0]` にする → ATR 2 の 3 件など 6 FAIL。W1C を handler の前に動かす → 「acknowledged after the handler」で FAIL。
- 試していない事: drain の待ち（`in_flight` が 0 でない間の disestablish）。core は handler の中からの解除を host に届く前に EBUSY で返すので、単一 thread の fake では作れない。形は p021 の fake host（試験 4、pthread で止めた）と同じ。bus 1 の下の bridge をまたぐ swizzle（host は bus 2 以下を持たない）。

## build（2026-10-19、BUILD は自分の worktree の build/p020-*）

| 構成 | command | 結果 |
| --- | --- | --- |
| VF2 | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p020-vf2 -j16 vmunix` | exit 0、warning 0・error 0（build/tmp/p020-vf2.log）。pci-plda.c・vf2-pcie.c・xHCI・HID・hub・NVMe を compile・link |
| riscv64 virt（PCI 入り、vmunix.mk の分け直しの確かめ） | `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/p020-virt -j16 vmunix` | exit 0、warning 0（build/tmp/p020-virt.log） |

- pci.c・fdt.c など amd64・arm64 と共有の file は変えていないので、amd64・arm64 の build はしていない。clang-format は host に無く未実施。`git diff --check` 0。

## 実機で見る行（ユーザーが framebuffer を目で見る。QEMU には PLDA が無いので実機だけ）

前提: p022（CRG・pinctrl の実機）と p023（coherency）の後。VF2 の image に USB keyboard と M.2 の NVMe（あれば）を挿す。boot の画面の kernel の log に、host ごとに次が出る。

1. `pcie0: PLDA at 2b000000, configuration 940000000, interrupt 56`、`pcie1: PLDA at 2c000000, configuration 9c0000000, interrupt 57`。
2. firmware の残した状態（Minor 12）: `pcie0: firmware left 1106:3483 class 0c0330, command 0006` の形（VL805 なら 1106:3483 の見込み、未確認。どちらの host が USB かもここで分かる）と `pcie0: xHCI USBSTS ……, HCHalted 0|1`。NVMe の host は `pcieN: NVMe CC.EN 0|1, CSTS.RDY 0|1`。M.2 が空なら `pcieN: firmware left the link up with no endpoint` か `firmware left the link down`。値を記録する（U-Boot が ExitBootServices で止めたか）。
3. `pcie0: link up`（PERST の後に学習し直した）。空の slot は `pcieN: link down`。
4. `pcie0: 01:00.0 BAR0 at 30000000 (4096 bytes)` の形（VL805）、NVMe は `pcie1: 01:00.0 BAR0 at 38000000 (16384 bytes)` の形。
5. xHCI と USB keyboard の行（pci-xhci・usb-hid の既存の log）。login prompt で USB keyboard から `root` と打って login でき、`uname -m` が `riscv64`。
6. NVMe があれば `nvme0n1` が出て、読み書き（p024 の範囲。ここでは識別まで）。
7. 出てはいけない行: `visionfive2: PCIe unavailable`、`visionfive2: PCIe root complex unavailable (…)`、`pcieN: BAR placement failed`、`interrupt … unavailable`、`still running after a second`、`PERST# not driven`、`pins not applied`。HAL_FATAL・panic（2 回目の EOI など）。
8. §2.4 の短い coherency の確かめを xHCI で 1 回（p023 の手順。p020 の実機の最初）。

## 残り

- 実機の確かめ（上の 1〜8、p022・p023 の後）と Q1 の判定。
- inbound の ATR の出典（Q1 へ: 64 bit に広げるなら出典の扱いをユーザーに聞く。今は 32 bit で足りる見込み）。
- 範囲外の観察: p014（P2）の visionfive2.c の変更と、この Phase の 3 か所（include・probe_soc の末尾・refresh・input_init）は merge で隣り合う可能性がある。
