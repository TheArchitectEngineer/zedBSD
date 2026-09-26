<!-- awesome-plan project=zedbsd record=ws048p002 -->

# ws048-p002: PCIe の root complex と PCI の rpi4 の backend

Phase ID: `ws048-p002`
Parent: [WS048](../ws.md)
Status: planned
Queue: —
HAL の承認: 不要（hal.h は変えない。`src/hal/arm64/space.c` の実装の修正だけ）

## 範囲

- FDT の reader（`src/drivers/generic/fdt.c`、`include/drivers/generic/fdt.h`）。design.md §11。
- arm64 の `hal_space_map_device()` を契約どおりに（design.md §4.1）。
- brcmstb の host bridge（`src/drivers/pci/pci-brcmstb.c`、`include/drivers/pci/pci-brcmstb.h`）: reset・link・window・
  config の access・資源の割り当て・map_bar・INTx（design.md §2〜§4、§8）。
- `src/kern/platform/rpi4.c` から起動（FDT の node が有効なときだけ）。`pci.c`・`dma.c` を arm64 の build に。
- `platform/arm64/vmunix.mk` の未解決 symbol の検査の awk の quote（p001 の付随の発見）。

範囲外: SSC（design.md §2.4）、MSI、VL805 の firmware（p003）、xHCI（p005）。

## 受け入れ条件

1. host 試験: FDT（実物の DTB と `status = "disabled"` の DTB と壊れた blob）、brcmstb の register model（design.md §13.1）が通る。
2. rpi4 の `make -j16` が warning 0。amd64 の vmunix の build も通る（`pci.c`・`dma.c` を変えた場合）。
3. `BOOT_MODE=raspi4b plan/tools/boot-test.sh` で login prompt（QEMU は PCIe が disabled なので driver は何もしない）。
4. 新しい file の `style-check.py` が 0 件。既存の file は件数が増えない。
5. 実機: 起動が止まらず、`lspci` に 01:00.0 1106:3483 が出る。**ユーザーの確認。行うまで未実施**（受け入れ 1〜4 で Phase を clear し、実機の結果は後で追記する）。
