<!-- awesome-plan project=zedbsd record=ws048p005 -->

# ws048-p005: xHCI を rpi4 で

Phase ID: `ws048-p005`
Parent: [WS048](../ws.md)
Status: uncleared（2026-09-27。p004 の承認を待たずにできる準備は済み。rpi4 の config で有効にするのは p004 の後）
Queue: 2026-09-27 ユーザー指示のサブエージェントの実行（worktree の branch）
HAL の承認: 不要（p004 の承認の後）
依存: p002、p003、p004

## 範囲

- `pci-xhci.c`・`usb.c` を arm64 の build に。config の表の platforms に `rpi4`、`config/ci/config-rpi4.mk` を menuconfig で作り直す。
- `src/kern/platform/rpi4.c`: USB の core、xHCI の登録、`kern_platform_refresh_devices()` の `drv_pci_xhci_probe_roots()`。
- VL805 の振る舞い（design.md §9）: Set TR Dequeue の DCS の出所、TRB の先読みと ring の置き方を確かめ、要れば直す。

## 受け入れ条件

1. rpi4 と amd64 の `make -j16` が warning 0。QEMU raspi4b と amd64（xHCI の USB boot）の `boot-test.sh` で login prompt。
2. xHCI を変えた場合は amd64 の xHCI の host 試験（plan/tools/driver-fragments を使うもの）が通る。
3. 実機: xHCI が起動し、`lsusb` に VL805 の中の hub（2109:3431）が出る。**ユーザーの確認。行うまで未実施**。

## 2026-09-27 の実行（準備）

p004（非 coherent な DMA）の HAL の差分が承認されるまで、rpi4 で xHCI を動かしても coherent な確保が ENOTSUP で失敗する
（p004 で当てた `dma.c` の振る舞い。cache の効いた memory を黙って渡さない）。そこで、有効にする前にできることだけを行った。
`config/ci/config-rpi4.mk` の xHCI・HID・hub は `n` のまま（rpi4 の image は変わらない）。

| file | 内容 |
| --- | --- |
| `platform/arm64/vmunix.mk` | `CONFIG_DRIVER_PCI_XHCI`・`CONFIG_DRIVER_USB_HID`・`CONFIG_DRIVER_USB_HUB` で `pci-xhci.c`・`usb-hid.c`・`usb-hub.c` と USB の core（`usb.c`）を arm64 の build に入れる |
| `src/drivers/platform/rpi4/rpi4-pcie.c`・`.h` | 列挙の前に USB の core・HID・hub・xHCI の driver を登録（config で選んだものだけ）。`drv_rpi4_pcie_refresh()` で xHCI の root port を見る |
| `src/kern/platform/rpi4.c` | `kern_platform_refresh_devices()`（割り込みの後）から `drv_rpi4_pcie_refresh()` |

VL805 について知られている振る舞いの確認（design.md §9。code を読んで）:

| 項目 | 今の xHCI の driver | 結論 |
| --- | --- | --- |
| endpoint context の DCS が停止の後に正しくないことがある | Set TR Dequeue の DCS は driver の ring の記録（`ring->cycle`）から作る（`pci-xhci.c` の 2 か所） | 影響しない |
| TRB の先読み（segment の終わりを越える） | ring は 4 KiB の coherent な確保（256 TRB） | IOMMU が無いので次の物理 page を読むだけ。害は無い（実機で確認は未実施） |
| 64 bit の register（DCBAAP・CRCR・ERSTBA） | `wr64()` は下位 32 bit を先に、上位を後に書く | 32 bit の access だけで足りる。brcmstb の outbound への access も 32 bit |
| 割り込み | MSI-X → MSI → INTx の順に試す | brcmstb の backend は MSI を ENOTSUP で断り、INTx（INTID 175、level）になる |

## 検証（準備）

| 検証 | 結果 |
| --- | --- |
| `make -j16 vmunix CONFIG_DRIVER_PCI_XHCI=y CONFIG_DRIVER_USB_HID=y CONFIG_DRIVER_USB_HUB=y`（rpi4） | 成功、warning 0。xHCI の symbol が 60 個 link された |
| その kernel の QEMU raspi4b の boot test | login prompt（[qemu-login-usb-built.png](qemu-login-usb-built.png)）。QEMU に PCIe が無いので何も attach しない |
| 既定の rpi4 の config の build | 成功、warning 0（xHCI は入らない） |
| `style-check.py` | `rpi4-pcie.c`・`.h` 0 件、`rpi4.c` 6 件（前と同じ） |

## 再開の条件と残り

p004 の承認と適用の後:

1. `config/drivers/pci.drivers`・`usb.drivers` の platforms に `rpi4` を足し、menuconfig で `config/ci/config-rpi4.mk` を作り直す（xHCI・HID・hub を `y`）。
2. rpi4 の build・QEMU の boot test。
3. 実機: xHCI が起動し、`lsusb` に VL805 の中の hub（2109:3431）。**ユーザーの確認**。
