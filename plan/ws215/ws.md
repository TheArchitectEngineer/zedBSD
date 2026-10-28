<!-- awesome-plan project=zedbsd record=ws215 -->

# WS215: Raspberry Pi 4 の Bluetooth（内蔵の BCM43455 の Bluetooth）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p002〜p005 は実装・host 試験・build 済み、残りは Pi 4 の実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
<!-- retained-after-completion --> phase001 は source の comment（userland/base/bluetoothd/broadcom.c・broadcom.h、userland/firmware/brcmbt/Makefile・brcmbt-firmware.manifest）が設計の出典として参照している。消すか残すかは Q1 の判断待ち。
Completed: 2026-10-27（q1608）
Primary Milestone: MG005
Related Milestones: MG006
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Pi 4 の内蔵の Bluetooth（BCM43455 の Bluetooth の部分、PL011 の UART の HCI）で、zedBSD の既存の Bluetooth の stack（bluetoothd、/dev/bluetooth0、WS197・WS205 の機能）が動く。

## ユーザーの決定

- 2026-10-12（由来）:「描画の最適化の後、計測を取ったら、SMPを入れます。さらに計測をとって、最適化を終了したいです。そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」（WiFi は WS206、Bluetooth はこの WS）
- 2026-10-13: p001 の判断 D1〜D4 はユーザーが回答（D1 で `CONFIG_DRIVER_BCM2711_BT` の driver の表の既定 y、D2 の firmware の package）。D5 は未回答で推奨の (a) で進めた。

## 結果

- p002: arm64 で Bluetooth の userland（bluetoothd・bt・bt-probe）と kernel の `CONFIG_INPUT_BRIDGE` の build（warning 0、host 試験 PASS）。
- p003: kernel の PL011 の UART の HCI の transport（`rpi4-bt.c`、H4 の assembler、`BT_IOC_SET_SPEED`、config.txt の生成）。T1-566（QEMU raspi4b の既定と serial、amd64 の bt-loopback）PASS。
- p004: firmware の package（`userland/firmware/brcmbt/`、BCM4345C0.hcd）と license の本文の一致の監査。
- p005: bluetoothd の Broadcom の始動（reset、速さの切り替え、patchram、BD_ADDR）、host 試験 PASS。
- Pi 4 の実機の確かめ（firmware・3 Mbaud・scan・`bt show`、keyboard・mouse・audio・phone）は未実施。`config/ci/config-rpi4.mk` は `CONFIG_DRIVER_BCM2711_BT := n`（2026-10-28 に確かめた。driver の表 `config/drivers/architecture/arm64.drivers` の既定は y）。

## 制限・移管

- 実機の確認は未実施（p002〜p005 の Pi、p006 の統合）。
- p008（UART の SCO と HFP の通話）は HFP（[WS197](../ws197/ws.md) の p007b）と一緒に扱う。
- p007 の規約の全文は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」、2026-10-27 の精査で一括の見直しに読み替え）。
- 回帰に残す試験（`bt-broadcom-host-test`、`brcmbt-firmware-package-test.sh`）は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws215-p001 | 調べと設計（review-1・review-2） | in-progress（第 2 版と第 3 版の補い）のまま閉じた。実装は p002〜p005 |
| ws215-p002 | arm64 の Bluetooth の userland と input bridge の build | in-progress のまま閉じた（build・host 試験 PASS、Pi は未実施） |
| ws215-p003 | kernel: PL011 の UART の HCI の transport、UAPI、config.txt | in-progress のまま閉じた（T1-566 PASS、Pi の smoke は未実施） |
| ws215-p004 | firmware の package（brcmbt）と license の監査 | in-progress のまま閉じた（package・host 試験・本文の一致まで） |
| ws215-p005 | bluetoothd の Broadcom の始動 | in-progress のまま閉じた（host 試験 PASS、Pi は未実施） |
| ws215-p006 | 実機の統合（keyboard・mouse・audio・phone・WiFi との共存） | 未着手で閉じた（実機の確認だけ、2026-10-27 ユーザーの原文） |
| ws215-p008 | UART の SCO と HFP の通話 | 未着手。WS197 の HFP と一緒に |
| ws215-p007 | 規約の全文の見直し | WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・結果・試験は削除する（上の retained の phase001 は Q1 の判断の後）。git の commit `fb1f6afee` の `plan/ws215/` にある。Queue の履歴は [plan/history](../history/index.md)。
