<!-- awesome-plan project=zedbsd record=ws215 -->

# WS215: Raspberry Pi 4 の Bluetooth（内蔵の BCM43455 の Bluetooth）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-12 Q1 作成。2026-10-13 P3: p001 の第 2 版を review-2 が「p002 可、p003 は条件つき」と判定、第 3 版の補いで中-B〜F・軽微を入れた。p002 を実行（build・host 試験済み、T1 と実機の確かめ待ち）。p004 を実行（brcmbt-firmware の package、host 試験済み）。P2: p003 を実行（kernel の PL011 の transport、build warning 0・host 試験 PASS、T1 と実機の確かめ待ち））
Primary Milestone: MG005
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: P3 の lane（2026-10-13 Q1 の割当、p001・p002・p004）、P2 の lane（p003）
Resume point: p002（[phase002/phase.md](phase002/phase.md)）は T1 の QEMU raspi4b の起動の回帰と Pi の実機の確かめ（bluetoothd が静かに待つ）の結果待ち。p003（[phase003/phase.md](phase003/phase.md)）は実装・build・host 試験まで済み、T1 の QEMU raspi4b の起動の回帰と Pi の実機の smoke（bt-probe）の結果待ち（D1 で `CONFIG_DRIVER_BCM2711_BT` の既定 y、D5 は未回答で推奨の (a)）。p004（[phase004/phase.md](phase004/phase.md)）は package・host 試験・license の監査まで済み、rpi4 の image の build で file が載る確かめと Q1 の判定待ち。p005 は p003・p004 の後。
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
| [p001](phase001/phase.md) | 調べと設計 → design-reviewer（[review-1](phase001/review-1.md)、[review-2](phase001/review-2.md)） | in-progress（第 2 版 + 第 3 版の補い（review-2 への対応）、Q1 の判定待ち。D1〜D4 答え済み、D5 未回答） | — |
| [p002](phase002/phase.md) | arm64 で Bluetooth の userland（bluetoothd・bt・bt-probe）の build と、kernel の `CONFIG_INPUT_BRIDGE` の arm64 の規則と rpi4 の既定 | in-progress（build warning 0・host 試験 PASS、T1 の QEMU raspi4b と実機の確かめ待ち） | p001 の review-1 の判定で開始可 |
| [p003](phase003/phase.md) | kernel: PL011 の UART の HCI の transport（`rpi4-bt.c`、HUNT・SYNC と自己 reset）、H4 の assembler、class・UAPI の変更（`BT_IOC_SET_SPEED`・`bt_info` の拡張）、config.txt の生成（`CONFIG_DRIVER_BCM2711_BT`、D1 で既定 y）、bt-probe の Read Local Name。実機の smoke | in-progress（P2、build warning 0・host 試験 PASS、test-wait: T1 の QEMU raspi4b と実機の smoke） | p002、ws206-p002a（GPIO・mailbox の GPIO の helper）、p001 の再 review の通過、値の出典の表の p003 の行、D1・D5 |
| [p004](phase004/phase.md) | firmware の package（`userland/firmware/brcmbt/`、BCM4345C0.hcd）と license の監査（本文の一致） | in-progress（package・host 試験 PASS・本文の一致を確かめた。rpi4 の image の build で file が載る確かめと Q1 の判定待ち） | p001、D2（答え済み、p003 と並行可） |
| p005 | bluetoothd の Broadcom の始動（始めの reset、速さの切り替え、patchram、BD_ADDR）、host の試験、実機で firmware・scan | planned | p003、p004、値の出典の表の p005 の行、D3 |
| p006 | 実機の統合（keyboard・mouse・audio・phone・WiFi との共存） | planned | p005（WS206・WS205 の統合の後の部分あり） |
| p007 | 規約の全文の見直し | planned | p006、p008 |
| p008 | UART の SCO（送りの拍か Synchronous Flow Control、`0xFC1C` の routing）と HFP の通話 | planned | p005、ws197-p007b |
