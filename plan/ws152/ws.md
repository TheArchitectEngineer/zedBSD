<!-- awesome-plan project=zedbsd record=ws152 -->

# WS152: system の更新（Settings の Updates の頁の実体、ベータ3）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG007
Related Milestones: MG003、MG006
Parent: [Master](../master.md)
Queue: なし（ベータ3 の実装の項目）
Resume point: p001 の検討の第 2 版（2026-10-06 P2、design-reviewer の review を反映）。推奨は案 B（A/B の system ＋ data）。§7 の判断 U1〜U12 待ち。決まったら §8 の p002（確かめ）から。
Target: **ベータ4 以降**（2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」）
<!-- awesome-plan-current:end -->

## 単一目標

Settings の Updates の頁（今は stub）から、zedBSD・Keiland の system を新しい版へ更新できるようにする。

## ユーザーの指示（2026-10-04 夜）

「SettingsのUpdatesは、ベータ3で実装するために、WSを作って、方式検討のPhaseを作っておいてください。」

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws152-p001](phase001/phase.md) | 方式の検討（更新の単位・配布・検証・適用・失敗の回復・UI） | planning（第 2 版、U1〜U12 待ち） | ベータ3 の計画、release の仕組み（WS129） |
