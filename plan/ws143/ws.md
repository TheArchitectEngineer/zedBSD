<!-- awesome-plan project=zedbsd record=ws143 -->

# WS143: Bluetooth（Settings の Bluetooth の頁の実体、ベータ2）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-05 P1 が p001 に着手、q752）
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: q752（P1、2026-10-05、p001）
Resume point: p001（調査と設計）を実行中。
Target: **ベータ2**（2026-10-05 の朝の user の「ベータ4以降」の後、同日の再編「ベータ3とベータ3の内容を、ベータ2に移動します」で WS143 を含むベータ3・ベータ4 以降の項目をベータ2 に移した（master の記録、Q1 の確認）。前の指示: user「WS037, WS044,WS048,WS141, ... WS143, ... は、ベータ4以降としてください。」）
<!-- awesome-plan-current:end -->

## 単一目標

Settings で stub になっている Bluetooth の頁を実体にし、zedBSD で Bluetooth の device（まず 5330 の AX211 の Bluetooth の半分、USB 8087:0033）を見つけ、pairing・接続・切断ができるようにする。

## ユーザーの指示（2026-10-04 夜）

「SettingsでスタブになってるBluetoothは、ベータ2（時期未定）での実装項目として、WSだけ作っておいてください。」

## 範囲（p001 で設計して確定）

- kernel: Bluetooth の HCI の transport（USB の btusb に当たる物、AX211 の Bluetooth の firmware の load）、HCI の core。
- userland: Bluetooth の daemon（pairing・鍵の保存・接続の管理）と CLI。
- desktop: libkeiland-backend の Bluetooth の口、compositor の拡張（kl_system_manager_v1）、Settings の Bluetooth の頁（今の stub を置き換え）、system bar の表示。Linux・FreeBSD の Keiland では各 OS の Bluetooth の仕組み（BlueZ など）を backend で包む。
- 最初の profile の範囲（HID のキーボード・マウス、音声の A2DP など）は p001 でユーザーと決める。
- 外部の実装・license の境界（Guardrail・設計方針）。firmware は userland/firmware の規約。

## Phase（案）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws143-p001](phase001/phase.md) | 調査と設計（device・firmware・HCI・profile の範囲・desktop の経路・試験の方法） | in-progress（q752） | — |
