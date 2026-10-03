<!-- awesome-plan project=zedbsd record=ws048p001 -->

# ws048-p001: 調査と設計

Phase ID: `ws048-p001`
Parent: [WS048](../ws.md)
Status: cleared（2026-09-27）
Queue: 2026-09-27 ユーザー指示（サブエージェントで WS048 に取り組む）。main session の Queue の外で、worktree の branch で実行。

## 目的

BCM2711 の PCIe（brcmstb）の初期化、window と DMA の番地、VL805 の firmware、割り込み、cache、xHCI・USB の driver の再利用、
PCI の層の rpi4 の backend、試験の方法を調べ、設計と Phase を書く。HAL の承認が要る所を分ける。

## 受け入れ条件

1. [design.md](../design.md) に初期化の手順・番地・DMA・VL805・割り込み・再利用・試験・Phase を書く。Linux の code を写さない。
2. HAL の承認が要る差分を特定し、hal.h の差分の案を書く。
3. 後続の Phase を依存と承認の要否つきで分ける。

## 調べたこと

- 実物の DTB（`vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb`、`dtc` で展開）: PCIe の register・outbound/inbound window・
  INTx の `interrupt-map`・`brcm,enable-ssc`・VL805 の node の `resets`・`dma-coherent` の不在。design.md §1。
- QEMU 10.0（`/usr/bin/qemu-system-aarch64`）の raspi4b は PCIe を持たない（`bcm2838-*` の device に PCIe が無い）。`-dtb` の DTB の
  `brcm,bcm2711-pcie` 他を `status = "disabled"` にする（binary の文字列 `bcm2711 dtc: %s has been disabled!` と compatible の一覧）。
- PCI の層（`src/drivers/pci/pci.c`）は host bridge を `drv_pci_bus_ops` で抽象化している。BAR と bus 番号は読むだけで割り当てない。
  arm64 の build には PCI・DMA・USB の core が入っていない。
- DMA の層（`src/drivers/generic/dma.c`）は coherent な bus だけを前提にする（sync は空、非 coherent の vector は拒む）。
- arm64 の `hal_space_map_device()` は対応を作らない（direct map の番地を返すだけ。4 GiB 以上の Pi の周辺機器の穴は Normal）。
  MAIR は Attr0 = Normal WB、Attr1 = Device-nGnRE、Attr2 = Device-nGnRnE で、Normal non-cacheable が無い。
- mailbox は HAL の中にあり、起動の framebuffer の設定だけが使う。FULL の確認の register が mailbox 0 の status（design.md §7 の注）。
- xHCI は 32 bit の MMIO だけを使い、TT の field を持つ。割り込みは MSI-X → MSI → INTx の順に試す。
- 実機の試験の仕組み（plan/tools の rpi4 の実機用の script）は無い。`boot-test.sh` の `BOOT_MODE=raspi4b` は QEMU だけ。
- arm64 の toolchain（`build/llvm/bin/clang --target=aarch64-unknown-zedbsd`）は動く。rpi4 の vmunix の build は 12 秒、warning 0。
- 付随の発見: `platform/arm64/vmunix.mk` の未解決 symbol の検査の awk が quote の誤りで動いていない
  （`awk: cmd. line:1: $1 == \"U\"`、検査は常に通る）。p002 で直す。

## 結果

- [design.md](../design.md)。
- HAL の承認が要るのは §6（非 coherent な DMA の `hal_pmem_map_uncached`・`hal_pmem_unmap_uncached`）だけ。device mapping（§4.1）は
  hal.h の契約どおりの実装の修正で、mailbox（§7）は driver が持つので、どちらも hal.h を変えない。
- Phase を p002〜p007 に分け直した（[WS048](../ws.md) の表）。旧 p002〜p005 は実行前（planning）だったので番号を振り直した。

## 検証

| 検証 | 結果 |
| --- | --- |
| DTB の値 | `dtc -I dtb -O dts` の出力で確認（design.md §1） |
| QEMU の PCIe の有無 | binary の文字列で確認 |
| 実機 | 未実施（この Phase は設計だけ） |
