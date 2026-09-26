<!-- awesome-plan project=zedbsd record=ws048p005 -->

# ws048-p005: xHCI を rpi4 で

Phase ID: `ws048-p005`
Parent: [WS048](../ws.md)
Status: planned
Queue: —
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
