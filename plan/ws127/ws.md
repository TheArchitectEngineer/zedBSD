<!-- awesome-plan project=zedbsd record=ws127 -->

# WS127: Files のベータ1 のブラッシュアップ（最重点）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared か canceled（p007 は 2026-10-13 ユーザーの実機の確かめ「OKでした」）。旧: incomplete）
Primary Milestone: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-02、ベータ1）「標準アプリ：まんべんなくブラッシュアップします。Filesは最重点、Settingsも重点的にしましょう。」
窓の Files（`userland/desktop/files`、`/bin/files`）をベータ1 の品質に仕上げる。到達目標 F1〜F5: Files の回帰の全 PASS、p001 の通しの重い・中の不具合 0、ユーザーが選んだ改善の実装、5330 の実機の主な操作、変えた source の全文規約と boot test。desktop の icon（`files --desktop`）は WS094。

## 結果（2026-10-13 完了）

- 棚卸し（p001、[requirements.md](requirements.md)）と、ユーザーの採否（2026-10-02: BUG-140/141 と試験の直し、BUG-142 の調査、PDF の thumbnail と disk cache、DnD の自動 scroll と spring、Move To・Open in New Window、eject、縦の macOS 風の scroll bar）。
- 不具合の直し（p002）、volume の trash（p003）、thumbnail の cache の上限と壊れた PDF（p004）、tab の上の spring（p006）、host 試験の font の path（p009）。eject は WS132 p005 で Files に実装された。
- 2026-10-04 のユーザーの要望: breadcrumb のタップで path の入力と候補（p010）、Home は ~/ の一覧・dashboard は「Today」（p011）、Tags の削除（p012）。
- 2026-10-10 の UAT の double-tap drag の誤 open の直し（p013、e654733f1）。
- 全文規約と回帰（p008、T2-025 PASS 8/8、FreeBSD の backend-test の native build warning 0）。5330 の実機（p007）は 2026-10-13 ユーザーの実機の確かめ「OKでした」。

## 制限・移管

- 日本語の UI（p005）は canceled、WS158 に吸収。カラム・ギャラリーの表示（F-033）、network・cloud の drive（F-032）、検索の index（F-034）、system の Quick Look（F-040）は入れていない（ユーザーの判断・Future Work）。
- F4 の数値（1000 項目 ≤ 1000 ms、選択 ≤ 50 ms）の実機の計測の記録は無い。p007 はユーザーの目視の確かめ。
- p008 の「FreeBSD の build の分は 10/13 以降」は T2-025 の FreeBSD の native build の後の再確認の意味で、Phase としては残していない。
- 2026-10-06 UAT の BUG-220・221・233・234 は Bug Board で扱う。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws127-p001 | 棚卸し: 回帰の取り直し、spec との照合、実使用の通しと不具合の表、改善の候補 | cleared（2026-10-02、q595） |
| ws127-p002 | p001 で見つけた不具合の直し（重い・中） | cleared（2026-10-06 Q1、q616-i01） |
| ws127-p003 | 名前の衝突と Trash の残り（volume の trash） | cleared（q666、T1-077） |
| ws127-p004 | thumbnail の拡張（cache の上限と壊れた PDF） | cleared（q667、T1-081） |
| ws127-p005 | 日本語の UI の文言と名前の変更での IME | canceled（2026-10-05 夜、WS158 に吸収） |
| ws127-p006 | DnD の tab の spring | cleared（q666、T1-077） |
| ws127-p007 | 5330 の実機での操作と速さ | cleared（2026-10-13 ユーザーの実機の確かめ「OKでした」） |
| ws127-p008 | 全文規約と回帰（WS の最後） | cleared（2026-10-08 Q1、T2-025 PASS 8/8） |
| ws127-p009 | Files の host 試験の font の path | cleared（q656） |
| ws127-p010 | breadcrumb のタップで path の入力と候補 | cleared（2026-10-05 Q1） |
| ws127-p011 | Home は ~/ の一覧、dashboard は Today | cleared（2026-10-05 Q1） |
| ws127-p012 | Tags の機能の削除 | cleared（2026-10-05 Q1） |
| ws127-p013 | double-tap drag の誤 open の直し | cleared（2026-10-10、e654733f1） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
