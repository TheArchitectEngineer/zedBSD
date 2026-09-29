<!-- awesome-plan project=zedbsd record=ws097 -->

# WS097: GTK4 の互換の実装（完全な書き下ろし、zlib）

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

- GTK4（と要る範囲の GLib・GObject・GDK）の公開の API に合わせた、完全な書き下ろしの実装。GTK の source は使わない（API の interface だけ）。
  license は zlib。**デモの後**。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws097-p001 | 設計 | planning | — |
