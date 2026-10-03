<!-- awesome-plan project=zedbsd record=ws048p006 -->

# ws048-p006: USB の hub と HID キーボードで console に入力

Phase ID: `ws048-p006`
Parent: [WS048](../ws.md)
Status: planned
Queue: —
HAL の承認: 不要
依存: p005

## 範囲

- `usb-hub.c`・`usb-hid.c` を rpi4 で有効に（config）。
- キーボードの入力が console の tty に入ること（`src/drivers/generic/console.c` の経路）。シリアルの入力と共存すること。

## 受け入れ条件

1. rpi4 の `make -j16` が warning 0、QEMU raspi4b の `boot-test.sh` で login prompt。
2. 実機: USB 2.0（黒）と USB 3.0（青）の port の USB キーボードで login できる。**ユーザーの確認。行うまで未実施**。
