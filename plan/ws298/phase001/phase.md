<!-- awesome-plan project=zedbsd record=ws298-p001 -->

# ws298-p001 Intel VMD の driver の設計

- 親: [WS298](../ws.md)。Queue: q1613 / q1613-i01（担当 P1、2026-10-27）。
- Status: in-progress（設計の版 1 を書いた、design-reviewer の前）
- Disposition: normal

## 目的・受け入れ

VMD（8086:467f）の CFGBAR を ECAM として下の bus を列挙し、MEMBAR の窓に BAR を置き、子の MSI-X を VMD の MSI-X に付け替え、zedBSD の PCI の枠組みに root bus として載せる設計を書き、design-reviewer の指摘を反映する。

## 成果

- [design.md](../design.md) 版 1（2026-10-27）。主な出典は FreeBSD 15.0 の `sys/dev/vmd/vmd.c`（BSD-2-Clause）、照合と手順だけ Linux 6.12.5 の `drivers/pci/controller/vmd.c`。
- 結論: PCI core（`pci.c`・`pci.h`）と HAL は変えずに、既存の `drv_pci_bus_ops` の hook（config・map_bar・allocate_irqs・msi_establish）で載る。割り当ては純な関数 `pci-vmd-plan.c` に分けて host 試験する。
- 判断の項目 U1〜U9（design.md §11）。

## 未実施・制限

- 5330 の VMD の BAR・MSI-X の数・VMCAP/VMCONFIG の値は未読（5330 は P3 が q1612 で使用中）。p002 の最初に読む（design.md §2.3）。
- Intel の公開の register の文書は未確認（U9）。
- design-reviewer: 未実施。
