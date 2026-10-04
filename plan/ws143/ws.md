<!-- awesome-plan project=zedbsd record=ws143 -->

# WS143: Bluetooth（Settings の Bluetooth の頁の実体、ベータ2）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: なし（ベータ2 の実装の項目、時期は未定）
Resume point: 未着手。ベータ2 の計画の時に p001（調査と設計）を Queue に入れる。
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
| ws143-p001 | 調査と設計（device・firmware・HCI・profile の範囲・desktop の経路・試験の方法） | planning | ベータ2 の計画 |
