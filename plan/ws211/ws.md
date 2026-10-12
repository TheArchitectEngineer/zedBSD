<!-- awesome-plan project=zedbsd record=ws211 -->

# WS211: Pi 4 の compositor の描画の削減（App Home の遷移のクロスフェード、draw の束ね）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: P3（WS141 の Pi の直しの次）
Resume point: p002（クロスフェード）・p003 段 1・p005 は main（Pi で確認中）。p006 はマウス移動の cursor の間隔の直し（main 1d9c9749e）。p007（dock・undock を窓の絵の拡大縮小に、configure はアニメの後）を実装（agent/p3 902a5feb2）、place16 で Pi の確認待ち（2026-10-12 P3）。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー: App Home を開くアニメが 2 コマしか出ない（「もうちょっと出そう」）。Pi 4 の kernel の log では App Home の 1 コマが 458〜916 draw（アイコン 1 つに光・絵・映り込み・名前の文字 1 字ずつ・動作中の線、床・時計・点・後ろのデスクトップの window ごと）で、1 コマ 150〜400 ms、開くアニメ（HOME_OPEN_MS 280）の間に 1〜2 コマ。アイコンの絵は起動時に atlas へ読み込み済み（glass.c tiles_create）で、時間差は表示の演出（HOME_CONTENT_STEP_MS）。ユーザー「場合によっては、デスクトップ1枚とHome1枚でクロスフェードすることが必要かも。」「App Homeのアイコンは、遷移のスムーズさのために常に保持するしかないと思います。」→ Q1 が (1) クロスフェード (2) draw の束ねの新しい WS を提案、ユーザー「承認します。」

## 目標

Pi 4 で App Home の開閉のアニメが 60 fps に近く、App Home を開いたままの操作（hover・ページ）も軽い。i915・Venus の見た目と速さを損なわない。

## 範囲

- (1) クロスフェード: デスクトップの絵と App Home の絵を 1 枚ずつ GPU の画像に持ち、開閉のアニメの間は 2 枚を拡大・縮小・透明度で重ねるだけ（1 コマ数 draw）。App Home の絵は常に保持し、アプリの一覧・動作中の印・ページ・言語・外観が変わった時だけ後ろで描き直す（最初の 1 コマも待たない）。アイコンの時間差の演出はやめるか置き換える（ユーザーと決める）。
- (2) draw の束ね: 同じ種類（文字の glyph、アイコンの tile、影・光、塗り）を 1 回の draw（instance か頂点の列）にまとめる。App Home とデスクトップの両方。
- compositor（userland/desktop/wayland/）の中。kernel・libvulkan・UAPI・HAL は変えない（必要が出たら WS141 へ）。

## 完了の条件（仮、p001 で確定）

Pi 4 で App Home の開閉が目で滑らか（ユーザーの確認）、1 コマの draw の数と時間が p001 の目標以下（実機の log）。i915・Venus で見た目が今と同じ（T1）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調査と設計（draw の数え、クロスフェードの絵の持ち方と無効化、束ねの形、Pi の見積もり、試験） | planning（第 4 版、review-4 待ち） |
| p002 | クロスフェードの実装と Pi の確かめ | planned |
| [p003](phase003/phase.md) | draw の束ね（文字・tile・影）と Pi の確かめ | 段 1 実装（未 merge）、段 2 は床の試しから |
| [p005](phase005/phase.md) | 時計の 1 分ごとの更新を時計の所だけに | in-progress（実装済み、Pi 未確認） |
| [p006](phase006/phase.md) | 窓の drag の移動とマウス移動を 60fps に近づける | in-progress（cursor の present の間隔の直しは main、drag は計測待ち） |
| [p007](phase007/phase.md) | dock・undock のアニメを窓の絵の拡大縮小にし、終わってから client に描き直させる | in-progress（実装、place16 で Pi 待ち） |
| p004 | 規約の全文の見直し | planned |
