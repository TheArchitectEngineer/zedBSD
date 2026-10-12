<!-- awesome-plan project=zedbsd record=ws215 -->

# WS215: Raspberry Pi 4 の Bluetooth（内蔵の BCM43455 の Bluetooth）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成。2026-10-13 P3 が p001 の設計の第 1 版を書いた、design-reviewer 待ち）
Primary Milestone: MG005
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: P3 の lane（2026-10-13 Q1 の割当、p001）
Resume point: p001 の設計（[phase001/phase.md](phase001/phase.md)）の review と判断 D1〜D4 の後、p002（arm64 で Bluetooth の userland を build）から。
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

設計の第 1 版（[p001](phase001/phase.md) の 7 節）の段の分け。計 約 15 LW。

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計 → design-reviewer | in-progress（第 1 版、review と D1〜D4 待ち） |
| p002 | arm64 で Bluetooth の userland（bluetoothd・bt・bt-probe・input bridge）を build | planned |
| p003 | kernel: PL011 の UART の HCI の transport（`rpi4-bt.c`）、H4 の assembler、GPIO・mailbox の GPIO の共通の helper（WS206 と共有）、UAPI（`BT_IOC_SET_SPEED`・`bt_info` の拡張）、config.txt の生成。実機の smoke | planned |
| p004 | firmware の package（`userland/firmware/brcmbt/`、BCM4345C0.hcd）と license の監査 | planned |
| p005 | bluetoothd の Broadcom の始動（速さの切り替え、patchram、BD_ADDR、SCO の routing）、host の試験、実機で firmware・scan | planned |
| p006 | 実機の統合（keyboard・mouse・audio・phone・WiFi との共存） | planned |
| p007 | 規約の全文の見直し | planned |
