<!-- awesome-plan project=zedbsd record=ws256-p012 -->

# ws256-p012 周辺の設計（VisionFive 2）

Status: cleared（2026-10-19 Q1 の判定: レビュー 3 の結論「Major 1・2 を直す小さな改訂の後なら受け入れてよい」に対し、改訂 4（aa4b92287）で Major 1・2 と Minor 1〜13 を当てた。ユーザーに聞く判断 §6.2・#29 は残る）

- 親: [WS256](../ws.md)。Queue: q1270-i01、q1270-i02（P1）、q1272-i01、q1278-i01（P2）。
- 範囲: p011（framebuffer の調べ）と p013〜p019（clock・reset・pinctrl/GPIO・PMIC、SD、rootfs からの起動、PCIe と USB・keyboard、NVMe、GbE と PHY、sound）の設計を `plan/ws256/design-peripherals.md` に書く。code は書かない。HAL の API の差分は案だけ。toolchain は変えない。
- 完了の条件: 部品と register の事実を StarFive の公開資料で確かめ（URL・SHA-256 を記録、確かめられない事は未確認）、各 Phase の既存／新規・HAL・DT・試験・依存・LW・並行の組、実機の判定（#15）と接続（#10）の選択肢と既定の案がそろい、design-reviewer を通り、Q1 が合格とする。

## 成果物

- [design-peripherals.md](../design-peripherals.md)（改訂 4）。[レビュー 1](../design-peripherals-review-1.md)、[レビュー 2](../design-peripherals-review-2.md)、[レビュー 3](../design-peripherals-review-3.md)。HAL の差分の案 [proposed/hal-riscv64-framebuffer.md](../proposed/hal-riscv64-framebuffer.md)（適用しない）。

## 実行した事（q1270-i01、2026-10-19）

- worktree `/home/awe/zedBSD-worktrees/p1`（agent/p1）に main ff7c0c597 を取り込んだ（fast-forward、`git log -1` = ff7c0c597）。子のエージェントは起動していない（Q1 の指示）。
- 取得（`build/tmp/ws256-docs/`、git に入れない、URL と SHA-256 は design-peripherals.md §0.1 と同じ directory の `SHA256SUMS`）: StarFive の JH7110 TRM（Preliminary V2）・JH7110 datasheet・VisionFive 2 datasheet、Linux v6.12 の JH7110・VF2 の DTS と binding の header、`sifive_ccache.c`、StarFive の配布の U-Boot と mainline v2025.01 の VF2 の defconfig、StarFive の SDK の README、OpenBSD の JH7110 の driver（ISC）。pdftotext で text にして grep した。
- 読んだ zedBSD の code: ws.md・design.md（改訂 2）・guardrail.md、`src/drivers/pci/pci-brcmstb.c` の構成、`src/drivers/platform/rpi4/rpi4-pcie.c`（VL805 の扱い）、`src/drivers/generic/dma.c`（coherent の確保）、`src/drivers/i2c/lpss-i2c.c`（DW I2C）、driver の directory の一覧。
- QEMU・実機は使っていない。build はしていない（code 無し）。

## 主な所見

- ws.md の p012 の行の部品の名前（Q1 の覚え）の照合: AXP15060（I2C5、0x36）・DesignWare MMC・Synopsys の Ethernet QoS・Motorcomm の PHY（YT8531）・PLDA の PCIe は公開資料で合った。**VL805 は公開資料に無く未確認**（U-Boot の構成から PCIe の先の xHCI は確か）。「XpressRICH」の名前は公開資料に無い。**sound は codec ではなく PWMDAC**（3.5 mm、DTS）。
- **design.md §1.6 の「JH7110 の DMA は非 coherent」は JH7100 の事実の取り違えの可能性が高い**（Linux の ccache の非 coherent の操作は jh7100 だけ、JH7110 の DTS に dma-noncoherent が無い）。推定は coherent で、p013 の実機で確かめる（design-peripherals.md §1.2、判断 #25）。
- p016 を p016a（QEMU virt の ECAM の host で既存の xHCI・HID・NVMe を riscv64 で試す、板の前に可）と p016b（PLDA、実機）に分ける案。
- HAL の API の変更は要らない見込み（coherency の推定が外れた時の予備は HAL の実装の変更だけ）。

## 残り

- design-reviewer（Q1 が回す）、その結果の反映（改訂）。
- ユーザーの判断（Q1 経由）: design-peripherals.md §6 の #15・#10・#2・#20〜#26。
- `build/tmp/ws256-docs/starfive,jh7110-crg.h`（重複の取得の残り）の片付けは Q1。

## 実行した事（q1270-i02、2026-10-19）

- worktree に main d89292d83 を取り込んだ（`git log -1 --format='%h %p'` = d89292d83 e4418e2ef、fast-forward）。子のエージェントは起動していない。
- 取得（`build/tmp/ws256-docs/`、SHA-256 は §0.1 と `SHA256SUMS`）: OpenBSD `sys/arch/riscv64/dev/sfcc.c`（jh7110 に attach しない）、Linux v6.12 の `arch/riscv/Kconfig`（`select ARCH_DMA_DEFAULT_COHERENT`）・`kernel/dma/Kconfig`・`drivers/of/address.c`（`of_dma_is_coherent`）・`drivers/of/device.c`。TRM の text で Table 2-5 の system port `RW A` を確かめた。
- 照合した code: `pci.c` の `establish_intx`（3037）と line の解除（3550）、`pci.h` の `drv_pci_bus_ops`・`drv_pci_irq`、`pci-nvme.c` の IRQ の許可（1738）と INTMS/INTMC、`design.md` §2.5。
- レビュー 1 の B1・B2・M1〜M8・Minor 1〜17 を全部当てた（対応の表は design-peripherals.md §9）。主な変更: PCI core の host の INTx の hook（新 Phase p021 仮）、pci-nvme の INTx の道（p017）、framebuffer の名前付き handoff の HAL の差分の案、coherency の確かめの Phase（p023 仮）と予備 (i) の具体化、p013 の code と実機（p022 仮）の分割、p016a/b を p016・p020（仮）に、mainline の U-Boot を主に。
- QEMU・実機・build は使っていない（code 無し）。

## 残り（q1270-i02 の後）

- 改訂 2 の design-reviewer（Q1 が回す）。
- Q1 への依頼: HAL の承認（#29）、design.md の直し 4 件（§8）、p020〜p023 の番号、tools（#28）。
- ユーザーの判断: design-peripherals.md §6（#15・#10・#2・#20〜#31）。

## レビュー 2（2026-10-19 Q1）

[design-peripherals-review-2.md](../design-peripherals-review-2.md): Blocker 1（p021 の hook の EOI）・Major 7・Minor 13。p016 はそのまま進めてよい、p017 の virt は Minor 1 を当てて進めてよい。改訂 3 は P1 が q1271 の後に書く。

## 実行した事（q1272-i01、2026-10-19、P2）

- worktree `/home/awe/zedBSD-worktrees/p2`（agent/p2）に main を取り込んだ（`git log -1 --format='%h %p'` = 3e3e053ed 19dd4f767、fast-forward）。子のエージェントは起動していない。新しい資料は取得していない（P1 の `build/tmp/ws256-docs/` を読むだけ）。
- 照合した code: `include/hal/hal.h:1284-1292`（`hal_get_arch_handoff`）、`src/hal/riscv64/bootinfo.c`・`cmain.c`・`page.c`・`space.c`・`fdt.h`・`irq.c`・`bsp.h`・`bsp-virt/`、`bootloader/include/riscv64-handoff.h`（p004、版 1）、`bootloader/uefi/video.c`、`include/kern/boot.h`・`platform.h`、`src/kern/pmem.c`、`src/hal/arm64/bsp-rpi4/boot.c`・`src/hal/amd64/bsp-pcat/boot.c`、`src/drivers/pci/pci.c`（establish・disestablish・dispatch）・`pci.h`、`pci-nvme.c`、`pci-brcmstb.c`、`src/drivers/generic/dma.c`。資料: jh7110.dtsi の ccache・pcie の node、mainline の U-Boot の defconfig、obsd-stfpcie.c。
- レビュー 2 の BL1・MJ1〜MJ7・Minor 1〜13 を全部当てた（対応の表は design-peripherals.md §10）。主な変更: p021 の hook を EOI を送らない `intx_line_run`・(host, vector) の鍵・lock の順と drain・戻り値で作り直した。p011 の土台を「描画は HAL（console を riscv64 の共通に）」「handoff 版 2（framebuffer と `reserved[]`）」「HAL が予約を外す」にし LW を 10〜14 に。proposed/hal-riscv64-framebuffer.md を hal.h の実際の形に直した。#20 を「配布の U-Boot で GOP が出た時に主をどちらにするか」に。coherency の確かめを 64 KiB・陽性の対照・8 回に、予備 (i) を確保ごとの別名の写像に（design.md §2.5 不変）、`vf2_dma_coherent()`。p024（p017 の実機）と p025（p005 の skeleton）を分けた。ws.md の表に p020〜p025 を planning で足し、p016・p017 の中身と他の行の依存・LW を合わせた（p016・p017 の状態の欄は変えていない）。
- QEMU・実機・build は使っていない（code 無し）。

## 残り（q1272-i01 の後）

- 改訂 3 の design-reviewer（Q1 が回す）。
- Q1 への依頼: HAL の承認 #29（文面は design-peripherals.md §6.3）、design.md の直し 7 件（§8）、tools（#28）、範囲外の観察（`hal_irq_unregister` が drain しない、§8）。
- ユーザーの判断: design-peripherals.md §6.2（#15・#10・#20・#21〜#31）。

## レビュー 3（2026-10-19 Q1）

[design-peripherals-review-3.md](../design-peripherals-review-3.md): Blocker 0・Major 2・Minor 13。結論「Major 1（p011 (1) の loader の前提）と Major 2（system port の別名の前提と 2 MiB の粒度）を直す小さな改訂の後なら受け入れてよい。p021・p025・p013 は今の設計で着手してよい（Minor 3・4・5・8 を当てる）」。

## 実行した事（q1278-i01 の 1、2026-10-19、P2、改訂 4）

- worktree `/home/awe/zedBSD-worktrees/p2`（agent/p2）に main 96ace9cfc を取り込んだ（`git log -1 --format='%h %p'` = 96ace9cfc 5318ccba0、fast-forward）。子のエージェントは起動していない。新しい資料は取得していない。
- 照合した code: `bootloader/uefi/bootriscv64.c`（`find_boot_hart` 708-733、`efi_main` 169-173、FDT の検査 770-773、`GetMemoryMap`・`ExitBootServices` 1001-1027）、`platform/riscv64/bootloader.mk:16-17`、`src/hal/riscv64/cmain.c:153-162`、`space.c`（`hal_space_map_device` 916-975、unmap 990-1010、`map_hal_devices`・`map_device_blocks` 1350-1389）、`bsp-virt/board.c:85-138`、`fdt.c`（ccache）、`include/hal/hal.h` の `HAL_ERR_*`、`include/drivers/generic/dma.h`、`src/drivers/generic/fdt.c`（`drv_fdt_parent`）、`pci.c:3520-3533`、`pci-nvme.c:1741-1746・5555-5558・2978-2980`、`riscv-virt.c`。
- 当てた物（対応の表は design-peripherals.md §11）: Major 1 → p011 (1) に loader の boot hart の代わりの道（`/chosen/boot-hartid`）と GOP の答え（LW +1〜2）。Major 2 → 陽性の対照に「別名が同じ DRAM である事」の 0a、INCONCLUSIVE・MISMATCH の時は主の試験だけで判定、予備 (i) は device の窓の別名の範囲に 4 KiB の段。Minor 1〜13 を全部。ws.md の p006・p011・p012 の行、表の見出し・実行の順・合計。
- QEMU・実機・build は使っていない（plan だけ）。

## 残り（q1278-i01 の 1 の後）

- Q1 の判定（改訂 4 を受け入れるか、再レビューを回すか）。
- Q1 への依頼とユーザーの判断は design-peripherals.md §8・§6.2 のまま（#15 に (D) JTAG を足した）。
