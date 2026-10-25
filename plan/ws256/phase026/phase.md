<!-- awesome-plan project=zedbsd record=ws256-p026 -->

# ws256-p026 PLDA の PCIe の MSI（VF2）

- 親: [WS256](../ws.md)。設計: [design-peripherals.md](../design-peripherals.md) §3.5・§3.6・§7（MSI の demux は Future Work だった）。
- 状態: planned（2026-10-26 06:21 Q1 新設、P2 が [p024](../phase024/phase.md) の後に）
- 目的: VF2 の M.2 の KC705（WS288 [p043](../../ws288/phase043/phase.md)、GHCI は MSI 1 つ、今の bitstream は MSI が無効だと割り込みを出さない）と NVMe で MSI を使う。

## 決定

- 2026-10-26 06:21 ユーザー「では、まだ時間があるので、MSIを実装したらどうでしょう。」→ Q1 の問い（PLDA の MSI の出典、WS256 design-peripherals の Minor 3「後で MSI を足す時は出典の扱いを改めて聞く」）の回答「Linux も値だけ参考」= OpenBSD sys/dev/fdt/stfpcie.c（ISC）の `#ifdef notyet` の MSI の部分に加え、Linux の drivers/pci/controller/plda/pcie-plda-host.c（GPL-2.0）の register の offset と bit の値だけを参照してよい（code は写さない）、出典は file の Value(s) referenced に記録。→ WS256 p026（PLDA の MSI）を新設、P2 が p024 の後に。

## 範囲

1. pci-plda.c: PLDA の MSI の受け口（MSI の address の窓、MSI の status・mask の register、1 本の PLIC の線の中の MSI の demux、vector の割り当て）。
2. PCI core: host の bridge が自分の MSI を出す道（drv_pci_bus_ops の hook など、kern_irq_register_msi の HAL の道の代わり）。HAL の API（include/hal/hal.h）を変えるなら差分ごとの承認（plan に差分を置く）。amd64・arm64・riscv64 virt の MSI と INTx の道は変えない。
3. 試験: host の試験（fake の host）、実機の NVMe を MSI で（INTx から MSI に変わる事、読み書きの一致は p024 の rw.sh）、後で KC705 の GHCI の MSI。

## 受け入れ

実機の NVMe が MSI で動き（log の MSI の表示、rw.sh の一致）、PCIe0 の VL805 と INTx の道も今までどおり。build の warning 0、変えた所の host の試験。

## 記録

- 2026-10-26 06:21 Q1: 新設。
