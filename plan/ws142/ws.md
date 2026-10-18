<!-- awesome-plan project=zedbsd record=ws142 -->

# WS142: デスクトップのアプリの切り替え（上部のバーのアプリの一覧とプレビュー、Alt+Tab・3 本指のタップの切り替え、Windows キー、タッチパッドのジェスチャー）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。p001〜p010 は 2026-10-08 までに全部 cleared、残っていた 5330 の触った感じと閾値の UAT は上の原文で確認済みの扱い。旧: incomplete）
<!-- retained-after-completion --> Phase の directory・試験は他の WS・plan/tools から参照されているので残す（2026-10-18 Q1、ユーザーの承認の案 (a)）。参照する試験を変える時に 2026-10-06 の基準で plan/tools へ移すか消す。
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

Keiland の compositor（`userland/desktop/wayland/`）で、ユーザーの要望（2026-10-04 夜、UAT-3 の後）のアプリの切り替えの操作を実装し、マウス・キーボード・タッチパッドの全部から同じ切り替えの UI に入れるようにする: Windows キーでアプリの一覧、タッチパッドの端からの 2 本指（Wiseview・仮想デスクトップ）、上部のバーのアプリの一覧とプレビュー、3 本指のタップ・Alt+Tab の切り替え。2026-10-06 の UAT のフィードバック（BUG-215・216・217・224・228）で、最大化の session の状態と gesture の体系を再設計した（p007〜p010）。

## 結果（2026-10-13 完了）

- Super 単独で App Home を開く・閉じる（p002）。
- タッチパッドの gesture（p003）と、その体系の再設計（p009: 上端の 2 本指、全画面の下端の 2 本指、WiseView・preview は 1 swipe で 1 つ）。
- 上部のバーのアプリの一覧とプレビュー（hover・click・複数の窓・drag の並べ替え、p004）。
- 切り替えの UI（Alt+Tab・3 本指の tap・2 本指、最大化の時は中央の popup、p005）。BUG-209（Alt+Tab の順）は別に直した。
- 最大化を desktop の session の状態にする（layout_mode と切り替えの入口の集約、p008）、中央の窓の下の層を 1 枚の blur に（p008b）。
- 規約の全文の確認と QEMU の回帰（p006、T1-136）、AAT のシナリオ・T1・規約（p010、T1-342・T1-359）。
- 5330 の触った感じと閾値の UAT: 2026-10-13 ユーザーの指示（上の原文）で確認済みの扱い。

## 制限・移管

- touchpad の閾値・触った感じの数値は実機の記録が無い（上の UAT の扱い）。
- touchpad の上端・下端の入れ替えと App Home を下へ払って閉じる操作は [WS181](../ws181/ws.md) の p012 が引き継いだ（p008・p009 の範囲の一部を置き換え）。
- 回帰に使う QEMU の試験（`tests/p003-guest.sh`・`tests/p010-guest.sh`）と host の gesture の試験は、tests/ のシナリオと WS181 p012 が参照している。移す先は Q1 が決める。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws142-p001 | 設計（状態機械・描画・入力の割り当て、D1〜D11） | cleared（2026-10-08 Q1） |
| ws142-p002 | Super（Windows キー）単独で App Home を開く・閉じる | cleared（2026-10-05 Q1） |
| ws142-p003 | タッチパッドの gesture（下の端・左右の端の 2 本指、代わりの 3 本指） | cleared（2026-10-05 Q1、T1-132） |
| ws142-p004 | 上部のバーのアプリの一覧とプレビュー | cleared（2026-10-05 Q1、T1-131） |
| ws142-p005 | 切り替えの UI（Alt+Tab・3 本指の tap・2 本指、中央の popup） | cleared（2026-10-05 Q1、T1-136） |
| ws142-p006 | 全文の規約の確認と QEMU の回帰 | cleared（2026-10-05 Q1、T1-136） |
| ws142-p007 | 設計: 最大化を desktop の session の状態にする、touchpad の gesture の体系 | cleared（2026-10-08 Q1） |
| ws142-p008 | 最大化を desktop の session の状態にする（layout_mode と切り替えの入口の集約） | cleared（2026-10-08） |
| ws142-p008b | 中央の窓の下の層を 1 枚の blur にまとめて描く | cleared（2026-10-08） |
| ws142-p009 | touchpad の gesture の体系 | cleared（2026-10-08） |
| ws142-p010 | AAT のシナリオ・T1 の試験・規約の見直し | cleared（2026-10-08 Q1、T1-342・T1-359） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
