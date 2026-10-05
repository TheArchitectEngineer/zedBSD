<!-- awesome-plan project=zedbsd record=ws097 -->

# WS097: GTK4 の互換の実装（完全な書き下ろし、zlib）

<!-- awesome-plan-current:start -->
Status: canceled（2026-10-05 ユーザー「WSをキャンセル：WS034、互換GTK4の実装、互換Qt6の実装、WS007」）。以前: planning
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
2026-10-02（計画担当）: 前提の知見は WS114（Linux GTK4）と WS115（upstream GTK4 の zedBSD 移植）から受ける。ベータ1（fg019）の対象外。
Target: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「デモ当日以降でよいが、Qt6のcore, gui, widgetsと、GTK4について、完全書き下ろしの互換実装を行う。APIのインタフェースだけ利用させてもらう。ライセンスはzlib。」

- GTK4（と要る範囲の GLib・GObject・GDK）の公開の API に合わせた、完全な書き下ろしの実装。GTK の source は使わない（API の interface だけ）。
  license は zlib。**デモの後**。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws097-p001 | 設計 | planning | — |

## 2026-10-02 / upstream学習との関係

Event ws114-gtk-qt-port-projections-20261002: ユーザー回答「書き下ろし計画も残す」および追加指示により、完全書き下ろし/zlib/API interfaceのみ/デモ後の方針を保持。[WS114](../ws114/ws.md)のLinux標準GTK4互換性実測と[WS115](../ws115/ws.md)のzedBSD upstream移植で要るOS API・ライブラリ・Wayland/描画/portalの知見を得てからp001を再設計する。GTK upstream sourceをこのWSへ転用しない。Queue none、実装なし。

## キャンセル（2026-10-05）

ユーザー「WSをキャンセル：WS034、互換GTK4の実装、互換Qt6の実装、WS007」。Phase の履歴と証拠は残す。未完の Phase はこれ以上進めない。
