<!-- awesome-plan project=zedbsd record=ws143 -->

# WS143: Bluetooth（Settings の Bluetooth の頁の実体）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: 2026-10-10 ユーザー「Bluetoothはキーボード、マウスについて接続確認、利用可能であることを確認できました。」→ p008（5330 の UAT）cleared）
<!-- retained-after-completion --> phase003〜phase007 の phase.md は残す: bluetoothd・bt-hci-loopback・libkeiland の Bluetooth の source の comment が、これらの節を仕様として参照している（userland/base/bluetoothd/*.c・*.h、src/drivers/generic/bt-hci-loopback.c、userland/desktop/ の bluetooth-*.c ほか）。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Settings で stub になっている Bluetooth の頁を実体にし、zedBSD で Bluetooth の device（まず 5330 の AX211 の Bluetooth の半分、USB 8087:0033）を見つけ、pairing・接続・切断ができるようにする。最初の profile は HID（BR/EDR の HID と LE の HOGP のキーボード・マウス）。

## ユーザーの決定

- 2026-10-04 夜「SettingsでスタブになってるBluetoothは、ベータ2（時期未定）での実装項目として、WSだけ作っておいてください。」
- 2026-10-05 朝「WS037, WS044,WS048,WS141, ... WS143, ... は、ベータ4以降としてください。」→ 同日の再編「ベータ3とベータ3の内容を、ベータ2に移動します」で WS143 はベータ2 へ。
- 2026-10-05 夕: [design.md](design.md) §9 の D1〜D18 をユーザーが全部推奨どおりに決定（HID が先、A2DP・PAN は別、firmware の load と HCI core は userland の daemon（D15）など）。A2DP・PAN はユーザー「HIDが先で…」（F-082）。
- 2026-10-08 夕 UAT（5330）: ユーザー「Bluetooth is not available on this computerになっています。」（当時は UAT の image に bluetoothd が無く、intelbt の load が未実装だった）
- 2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p009 は WS の外の一括の見直しへ。
- 2026-10-10 ユーザー「Bluetoothはキーボード、マウスについて接続確認、利用可能であることを確認できました。」

## 結果

- kernel: USB の transport `bt-usb` と HCI の char device `/dev/bluetoothN`（`include/uapi/bluetooth.h`）、HID の入力の口 `/dev/input/bridge`、試験の loopback の controller（`src/drivers/generic/bt-hci-loopback.c`）。
- userland: `bluetoothd`（firmware の load、HCI・L2CAP・SMP・SDP・GATT・HID host（BR/EDR と HOGP）、pairing・鍵の保存、特権の分離）、CLI `bt`、firmware の package `intelbt`（`userland/firmware/intelbt`）。`config/current-uat.mk` に `bluetoothd bt intelbt-firmware`。
- desktop: libkeiland-backend の Bluetooth の口、compositor の拡張、Settings の Bluetooth の頁、system bar、pairing の確認の窓。Linux の backend（BlueZ、p007）。
- 証拠: QEMU の T1-384（p002）、T1-409・T1-426（p004）、T1-438・T1-446（p006）、Debian 13 の QEMU+KVM の T1-593・T1-604・T1-605（p007）。実機: 5330 でユーザーの BR/EDR・LE のキーボード・マウスの接続（2026-10-10、p008）。

## 制限・移管

- A2DP（音声）と PAN（network）は [future-work.md](../future-work.md) の F-082。スマホの profile は WS197。
- p003（5330 での firmware の load と scan の T1 の確かめ）と p005（i02・i03 の T1 の再試験、i04 の 5330 の実機の門）の phase.md は in-progress のまま WS を閉じた。p008 のユーザーの実機の確かめで代える。
- FreeBSD の backend は未対応の表示だけ。
- 規約の全文（p009）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws143-p001 | 調査と設計（device・firmware・HCI・profile・desktop の経路・試験） | cleared（D1〜D18 はユーザーが決定） |
| ws143-p002 | `bt-usb` と `/dev/bluetoothN`、試験の道具 `bt-probe`、loopback の controller | cleared（T1-384 QEMU PASS。5330 の passthrough は未実施） |
| ws143-p003 | firmware の package `intelbt`、bluetoothd の transport・firmware の load・HCI core・scan、`bt show`・`bt scan` | in-progress のまま WS を閉じた（実装は main、i01 は T1-402 PASS、5330 の load・scan の T1 は未実施。p008 で実機の接続を確かめた） |
| ws143-p004 | L2CAP、SSP、LE の SMP、暗号、鍵の保存、特権の分離、`_bluetooth` の account | cleared |
| ws143-p005 | usb-hid の glue の共有、`/dev/input/bridge`、SDP・GATT client、HID host（BR/EDR と HOGP） | in-progress のまま WS を閉じた（実装は main、i02・i03 の T1 の再試験は記録なし。p008 で実機の接続を確かめた） |
| ws143-p006 | desktop: backend の口、API と protocol の版、Settings の頁、system bar、pairing の窓 | cleared（T1-438・T1-446） |
| ws143-p007 | Linux の backend（BlueZ）、FreeBSD の未対応の表示 | cleared（T1-593・T1-604・T1-605） |
| ws143-p008 | UAT（5330、ユーザーの BR/EDR と LE のキーボード・マウス） | cleared（2026-10-10 ユーザーの確かめ） |
| ws143-p009 | 規約の全文との照合と回帰（directory なし） | 規約の全文は WS の外の一括の見直しへ |

## 記録の所在

設計は [design.md](design.md)。phase003〜phase007 の phase.md は上の理由で残す。他の Phase の記録は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws143/` にある。Bluetooth の host 試験と QEMU の試験の script は plan/tools/ へ移す（[plan/tools.md](../tools.md)）。2026-10-08 の緊急のラップアップの救出物 `wip-20261008/` は fb1f6afee に残る。Queue の履歴は [plan/history](../history/index.md)。
