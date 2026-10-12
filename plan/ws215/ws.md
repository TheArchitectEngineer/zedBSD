<!-- awesome-plan project=zedbsd record=ws215 -->

# WS215: Raspberry Pi 4 の Bluetooth（内蔵の BCM43455 の Bluetooth）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成。2026-10-13 P3 が p001 の設計の第 1 版、review-1 の後に第 2 版を書いた、design-reviewer の再 review 待ち）
Primary Milestone: MG005
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: P3 の lane（2026-10-13 Q1 の割当、p001）
Resume point: p002（arm64 で Bluetooth の userland と input bridge を build）は review-1 の判定で開始可。p003・p005 は p001 の第 2 版（[phase001/phase.md](phase001/phase.md)）の再 review、値の出典の表（2a 節）、判断 D1・D5（p003）・D2（p004）・D3（p005）、ws206-p002a（GPIO の helper）の main への統合の後。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「描画の最適化の後、計測を取ったら、SMPを入れます。さらに計測をとって、最適化を終了したいです。そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」WiFi は既存の WS206。Bluetooth はこの WS。

## 目標

Pi 4 の内蔵の Bluetooth（BCM43455 の Bluetooth の部分）で、zedBSD の既存の Bluetooth の stack（bluetoothd、/dev/bluetooth0、WS197・WS205 の機能）が動く。

## 範囲（p001 で決める）

- 接続の経路（PL011 の UART の HCI、今の config.txt は `dtoverlay=disable-bt` で UART を console に回している。WS141 で serial の console は既定で無効にした）、firmware（patchram の .hcd）の読み込み、clock・GPIO・電源の制御、device tree。
- 既存の USB の Bluetooth の driver と HCI の層の共有。
- WiFi（WS206、同じ chip の SDIO）との共存（coexistence）。
- 試験: Pi の実機（keyboard・mouse・audio・phone の連携）。

## Phase

設計の第 2 版（[p001](phase001/phase.md) の 7 節）の段の分け。計 約 18 LW。p007（規約の全文）は WS の最後（p006・p008 の後）。

| Phase | 内容 | Status | 依存・入る条件 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計 → design-reviewer（[review-1](phase001/review-1.md)） | in-progress（第 2 版、再 review と D1〜D5 待ち） | — |
| p002 | arm64 で Bluetooth の userland（bluetoothd・bt・bt-probe）の build と、kernel の `CONFIG_INPUT_BRIDGE` の arm64 の規則と rpi4 の既定 | planned | p001 の review-1 の判定で開始可 |
| p003 | kernel: PL011 の UART の HCI の transport（`rpi4-bt.c`、HUNT・SYNC と自己 reset）、H4 の assembler、class・UAPI の変更（`BT_IOC_SET_SPEED`・`bt_info` の拡張）、config.txt の生成（`CONFIG_DRIVER_BCM2711_BT`、既定 n）、bt-probe の Read Local Name。実機の smoke | planned | p002、ws206-p002a（GPIO・mailbox の GPIO の helper）、p001 の再 review の通過、値の出典の表の p003 の行、D1・D5 |
| p004 | firmware の package（`userland/firmware/brcmbt/`、BCM4345C0.hcd）と license の監査（本文の一致） | planned | p001、D2（p003 と並行可） |
| p005 | bluetoothd の Broadcom の始動（始めの reset、速さの切り替え、patchram、BD_ADDR）、host の試験、実機で firmware・scan | planned | p003、p004、値の出典の表の p005 の行、D3 |
| p006 | 実機の統合（keyboard・mouse・audio・phone・WiFi との共存） | planned | p005（WS206・WS205 の統合の後の部分あり） |
| p007 | 規約の全文の見直し | planned | p006、p008 |
| p008 | UART の SCO（送りの拍か Synchronous Flow Control、`0xFC1C` の routing）と HFP の通話 | planned | p005、ws197-p007b |
