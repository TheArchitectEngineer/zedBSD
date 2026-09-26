<!-- awesome-plan project=zedbsd record=ws048 -->

# WS048: Raspberry Pi 4 の USB（PCIe・VL805 の xHCI・USB キーボード）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG008
Related Milestones: MG003, MG006
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし（2026-09-27 ユーザー指示でサブエージェントが worktree の branch で実行。main session が merge する）
Resume point: p002（PCIe の root complex）。p004 は HAL の承認待ち（design.md §6）
<!-- awesome-plan-current:end -->

## 目標

実機の Raspberry Pi 4 で USB の Type-A の port が使え、USB キーボードで console に入力できる。

## きっかけ

2026-09-24、実機の RPi4 が login prompt まで起動した（ws044-p009）。ユーザー:「USBが使えないみたいです。コンフィグのせいでしょうか？」

調べた結果、config だけの問題ではない:

| 要るもの | 今 |
| --- | --- |
| BCM2711 の PCIe root complex（brcmstb）の初期化: reset、link の確立、outbound・inbound の window、config 空間の access | 無い。`src/drivers/pci` の PCI の層は PC の ECAM（`pci-pcat.c`）だけ |
| VL805 の firmware の読み込み: PCIe の reset の後に mailbox の `0x00030058`（xHCI の reset の通知）で VideoCore に読ませる | mailbox は HAL の中（`src/hal/arm64/bsp-rpi4/mailbox.c`）にしかない。driver から使うには HAL の口が要る（承認が要る） |
| xHCI（`src/drivers/pci/pci-xhci.c`） | ある。x86 の cache coherent な DMA を前提にしている。Pi 4 の PCIe の DMA は CPU の cache と coherent でないので、ring と buffer の cache の操作か非 cache の memory が要る |
| 割り込み: PCIe の INTx か MSI を GIC の SPI へ | 無い（polling で始める手もある） |
| USB の driver（HID・hub・storage） | ある。rpi4 の config で `n`（PCIe と xHCI が動くまで意味が無い） |

有線 LAN（GENET）も driver が無い。今の実機の入力はシリアルだけ。

## 設計

[design.md](design.md)（ws048-p001）。

## Phase 一覧

2026-09-27 の p001 で分け直した（旧 p002〜p005 は実行前だったので番号を振り直した）。

| Phase | 内容 | Status | 依存 | HAL の承認 |
| --- | --- | --- | --- | --- |
| [ws048-p001](phase001/phase.md) | 調査と設計 | cleared | — | 不要 |
| [ws048-p002](phase002/phase.md) | FDT の reader、arm64 の device mapping の実装の修正、brcmstb の host bridge と PCI の backend（VL805 が列挙される） | planned | p001 | 不要 |
| [ws048-p003](phase003/phase.md) | firmware の mailbox と VL805 の firmware の通知 | planned | p002 | 不要 |
| [ws048-p004](phase004/phase.md) | 非 coherent な DMA（`hal_pmem_map_uncached` と `dma.c`） | planned | p001 | **要る**（design.md §6） |
| [ws048-p005](phase005/phase.md) | xHCI を rpi4 で | planned | p002・p003・p004 | 不要 |
| [ws048-p006](phase006/phase.md) | USB の hub と HID キーボードで console に入力 | planned | p005 | 不要 |
| [ws048-p007](phase007/phase.md) | 規約の全文の確認と回帰、実機の結果の取りまとめ | planned | p002〜p006 | 不要 |

注: QEMU の raspi4b は PCIe を持たない（DTB の PCIe の node を disabled にする）。p002〜p006 の動作の確認は実機だけで、
このリポジトリに実機の試験の仕組みは無い。実機の確認はユーザーに頼み、行うまで「未実施」と書く。
