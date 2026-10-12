<!-- awesome-plan project=zedbsd record=ws215 -->

# WS215: Raspberry Pi 4 の Bluetooth（内蔵の BCM43455 の Bluetooth）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成、ユーザーの指示）
Primary Milestone: MG005
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: 未割当（Pi 4 の順: 描画の最適化 → 計測 → SMP（WS208）→ 計測 → 最適化の終了 → WiFi（WS206）と Bluetooth（この WS））
Resume point: 未着手。p001（調べと設計）から。
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

| Phase | 内容 | Status |
| --- | --- | --- |
| p001 | 調べと設計 → design-reviewer | planning |
| p002 | 規約の全文の見直し | planned |
