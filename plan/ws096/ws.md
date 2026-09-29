<!-- awesome-plan project=zedbsd record=ws096 -->

# WS096: Qt6（core・gui・widgets）の互換の実装（完全な書き下ろし、zlib）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「デモ当日以降でよいが、Qt6のcore, gui, widgetsと、GTK4について、完全書き下ろしの互換実装を行う。APIのインタフェースだけ利用させてもらう。ライセンスはzlib。」

- Qt6 の QtCore・QtGui・QtWidgets の公開の API（header の interface）に合わせた、完全な書き下ろしの実装。Qt の source は使わない（API の
  interface だけ）。license は zlib。Kei の Wayland・Vulkan・WS090 の部品の上。**デモの後**。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws096-p001 | 設計 | planning | — |
