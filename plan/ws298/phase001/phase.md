<!-- awesome-plan project=zedbsd record=ws298-p001 -->

# ws298-p001 Intel VMD の driver の設計

- 親: [WS298](../ws.md)。Queue: q1613 / q1613-i01（担当 P1、2026-10-27）。
- Status: cleared（2026-10-27。設計の版 2、design-reviewer の反映済み。§12 の D1・D2 は Q1 が決定: D1 (b)、D2 (a)）
- Disposition: normal

## 目的・受け入れ

VMD（8086:467f）の CFGBAR を ECAM として下の bus を列挙し、MEMBAR の窓に BAR を置き、子の MSI-X を VMD の MSI-X に付け替え、zedBSD の PCI の枠組みに root bus として載せる設計を書き、design-reviewer の指摘を反映する。

## 成果

- [design.md](../design.md) 版 1（2026-10-27、89cea113b）、版 2（同日、design-reviewer の 18 件を反映、[design-review.md](../design-review.md)）。主な出典は FreeBSD 15.0 の `sys/dev/vmd/vmd.c`（BSD-2-Clause）、照合と手順だけ Linux 6.12.5 の `drivers/pci/controller/vmd.c`。
- 結論: PCI core（`pci.c`・`pci.h`）と HAL は変えずに、既存の `drv_pci_bus_ops` の hook（config・map_bar・allocate_irqs・msi_establish）で載る。割り当ては純な関数 `pci-vmd-plan.c` に分けて host 試験する。
- 判断の項目 U1〜U9（design.md §11）。

## 未実施・制限

- 5330 の VMD の BAR・MSI-X の数・VMCAP/VMCONFIG の値は未読（5330 は P3 が q1612 で使用中）。p002 の最初に読む（design.md §2.3）。
- Intel の公開の register の文書は未確認（U9）。
- design-reviewer: 実施（2026-10-27）。重大 1（同じ PARTLABEL、D1）・中 5・軽 12。U1〜U9 に全部賛成（条件つき、版 2 に反映）。
- 人の判断待ち: D1（NVMe の区画の PARTLABEL、推し (b) 名前を変える）、D2（WS298 の目標を「見えて読める」に絞る、推し (a)）。design.md §12。→ 2026-10-27 Q1 が決定（D1 (b)、D2 (a)）。D1 は P1 が 5330 で実施。
