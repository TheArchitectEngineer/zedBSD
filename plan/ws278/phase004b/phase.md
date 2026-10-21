<!-- awesome-plan project=zedbsd record=ws278-p004b -->

# ws278-p004b RP1 の割り込み（MSI-X → MIP → GIC）

- 親: [ws278](../ws.md)、Queue: q1543 / q1543-i02（P3、2026-10-21）
- Status: in-progress（実装と host の確認まで。実機の確認は WS278 の最後の一括の試験）

## 目的と受け入れの条件

RP1 の周辺の割り込みを CPU に届け、USB の poll を割り込みに替える（p005 の Ethernet も使う）。

- build（warning 0）、`rpi5-rp1-host-test` で USB の 2 つの vector（31・36、edge）が MIP の最初の line（GIC の INTID 160・161）に向き、MSI-X の表の entry（doorbell 0xff_ffff_f000、message 0・1、unmask）と RP1 の vector の設定（ENABLE）が書かれる。
- 実機（一括の試験）: `rp1: interrupts through the MIP at fffffff000, lines 128-191, 61 vectors`、`rp1: vector 31 (edge) on line 160`・`vector 36 … 161`、`rp1-usb: polling 2 controllers every 50 ms`、keyboard の入力が遅れない。

## 設計

- `src/drivers/platform/rpi5/rp1-irq.c`: root complex の `msi-parent` の MIP（`brcm,bcm2712-mip`）の `msi-ranges`（GIC の SPI 128 から 64 本、edge）・`brcm,msi-offset`・2 つ目の reg（PCI 側の doorbell）を読み、MIP の register を写像する。MIP は VPU を mask、host の 64 本を edge・unmask（OpenBSD の bcmmip_attach と同じ手順）。
- RP1 の設定空間の capability の一覧から MSI-X を探し、表（BAR0）を写像し、全部の entry を mask、MSI-X を enable（function mask 無し）。RP1 の vector ごとの設定（周辺の窓の 0x108008 + 4×vector、SET/CLR の alias）を写像する。
- `drv_rp1_irq_establish(vector, trigger, handler, argument)`: 次の MIP の line を取り（返さない）、GIC の line を edge にして dispatcher を登録し、entry に doorbell と message を書いて unmask、RP1 の設定で level なら IACK_EN、ENABLE。dispatcher は handler → level なら IACK → EOI。
- `rp1-usb.c`: refresh で各 controller の `interrupts`（rp1 の 2 cell の指定）を routing し、割り込みから pin の service（PCI core の line）を呼ぶ。poll は残し、全部の controller に割り込みが付けば 50 ms（pin が mask の間に来た edge を落とさないため）、付かなければ 2 ms。
- 出典: OpenBSD の bcm2712_mip.c（MIP の register、手順）・rpone.c（MSIX_CFG の register と bit、level の IACK の手順）。

## 変えた所

- 新: `src/drivers/platform/rpi5/rp1-irq.c/h`。
- `rp1.c/h`（`drv_rp1_root_complex_node`）、`rp1-usb.c`（割り込みの routing、poll の間隔、`serve_function` を function の pointer に）、`tests/rpi5-rp1-host-test.c`（割り込みの確かめ）、`platform/arm64/vmunix.mk`（rpi5 の source と試験の規則）。

## 確認（2026-10-21 P3、host）

- rpi5 kernel build: rc=0、warning 0、vmunix check PASS。
- `rpi5-rp1-host-test`: C1・D0 とも PASS（vector 31・36 が line 160・161、表の entry、RP1 の ENABLE、範囲外の vector の拒否）。
- 未実施: 実機。

## 危険

- R10: MIP の line は返さない（establish の失敗でも消費）。64 本に対し使うのは USB 2・Ethernet 1 なので足りる。
- R11: firmware が MIP を VideoCore で使っていれば、VPU の mask で影響が出る（OpenBSD は同じ設定で動いている）。
