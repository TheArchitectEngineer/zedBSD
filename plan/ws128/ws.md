<!-- awesome-plan project=zedbsd record=ws128 -->

# WS128: 標準アプリ全般のベータ1 のブラッシュアップ

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared（p007 の 5330 の通し、p012 の UAT の image での icon の確認を上の原文で cleared）。旧: incomplete）
Primary Milestone: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-02、ベータ1）「標準アプリ：まんべんなくブラッシュアップします。」
Text Editor・Image Viewer・Notes・PDF Viewer・Terminal・音量・スクリーンキーボードなどをベータ1 の品質に仕上げる（Files は WS127、Settings は WS089、compositor と App Home は WS099、desktop の icon は WS094）。到達目標 A1〜A6: 各アプリの既存の回帰の全 PASS、p001 の通しの重い・中の不具合 0、UI に出ている未完成の機能 0、選んだ改善の実装、5330 の実機の通し、全文規約。

## 結果（2026-10-13 完了）

- 棚卸し（p001、[requirements.md](requirements.md)）。
- Notes の Open と Save As（p002）、Text Editor の Find & Replace と Open Recent（p003）、PDF Viewer の文字の検索と選択・copy（ToUnicode の抽出、p004）、Image Viewer の Move to Trash・Open With・slideshow（p005）、Terminal の scrollback の検索・色の theme と font の大きさの保存（p006）。
- Terminal: CJK Ambiguous Width の切り替え（p009）、右端の wrap を xterm と同じ保留に（BUG-150、p010）、IME の日本語の入力（BUG-155、p011）。
- App Home の app の icon をデザインした物に（montage-4、2026-10-06 ユーザーの決定、p012）。
- 全文規約と回帰（p008、T2-025 PASS 8/8、FreeBSD の backend-test の native build warning 0）。
- 5330 の実機の通し（p007）と p012 の UAT の image での確認: 2026-10-13 ユーザーの指示（上の原文）で確認済みの扱い。

## 制限・移管

- 画像の copy（Image Viewer）は F-074 へ移管（2026-10-04 Q1）。
- 既存の WS の残り（WS079 の S8・S9、WS090 の libkeiui への移行、WS100 の A7、WS102 の残り）はその WS で扱う。
- p007 の SSH の log の ERROR の確かめは未実施（ユーザーの目視の確かめだけ）。
- p008 の「FreeBSD の build の分は 10/13 以降」は T2-025 の FreeBSD の native build の後の再確認の意味で、Phase としては残していない。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws128-p001 | 棚卸し: 回帰の取り直し、menu の通しと不具合の表、未完成の UI、改善の候補 | cleared（2026-10-03、q623-i01） |
| ws128-p002 | Notes の Open と Save As | cleared（2026-10-03、q621-i01） |
| ws128-p003 | Text Editor の Find & Replace と Open Recent | cleared（2026-10-03、q621-i01） |
| ws128-p004 | PDF Viewer の文字の検索と選択・copy | cleared（2026-10-07、T1-288） |
| ws128-p005 | Image Viewer: Move to Trash・Open With・slideshow | cleared（q666、T1-078） |
| ws128-p006 | Terminal: scrollback の検索・色の theme と font の大きさの保存 | cleared（q666、T2-025） |
| ws128-p007 | 5330 の実機で標準アプリの通し（ユーザーの目視） | cleared（2026-10-13 ユーザー、上の原文） |
| ws128-p008 | 全文規約と回帰（WS の最後） | cleared（2026-10-08 Q1、T2-025 PASS 8/8） |
| ws128-p009 | Terminal: CJK Ambiguous Width の即座の切り替え | cleared（2026-10-03 Q1） |
| ws128-p010 | Terminal の右端の wrap（BUG-150） | cleared（2026-10-03、q636-i01） |
| ws128-p011 | Terminal で IME の日本語（BUG-155） | cleared（2026-10-03 Q1、T1-012） |
| ws128-p012 | App Home の app の icon をデザインした物に | cleared（2026-10-13 ユーザー、上の原文。montage-4 は 2026-10-06 ユーザーの決定） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
