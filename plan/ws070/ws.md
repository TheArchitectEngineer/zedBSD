<!-- awesome-plan project=zedbsd record=ws070 -->

# WS070: zdesktop の System Menu（`xdg_toplevel_menu_v1`、libzdesktop で包む）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: none（p001〜p004 は 2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
Resume point: p005（i915 実機、規約の全文との照合と回帰）。p001〜p004 cleared（Venus）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「この画像は、フローティングタイトルバーとドッキングタイトルバーにメニューを追加してみたものです。下記が仕様です。
X11サーバの実装が終わったら、OpenGLよりも、これを先に実装してもらえませんか？あとでGTK4やQt6のネイティブメニューバーとしても
利用可能にするつもりです。XDG拡張ではあるものの、libzdesktopでラップします。」

Wayland のクライアントがメニューの意味（階層・ラベル・状態・action・shortcut・role・icon name）を zdesktop に渡し、zdesktop が
システムの UI として描き操作する。通常の窓では浮いたタイトルバーに、最大化（docked）ではシステムバーに同じメニューを出す。
仕様案の原文と添付画像の説明は [spec.md](spec.md)。

## 方式

- protocol は仕様案の `xdg_menu_manager_v1`・`xdg_menu_v1`・`xdg_toplevel_menu_v1`（item は数値の ID、transaction の commit）。
  zdesktop の非標準の拡張なので、client は libzdesktop の API を使い protocol を直接話さない（2026-09-27 ユーザー決定）。
  後で GTK4・Qt6 の native menubar の backend がこの API（または protocol）を使う。
- zdesktop が描く: 浮いたタイトルバーの題名の右、docked のときはシステムバーの題名の右に top-level の項目（text だけ）、選ぶと
  zdesktop の popup（submenu、checkbox・radio、separator、shortcut の表示、keyboard の操作、外の click で閉じる）。
- 最初の使い手は zdesktop-terminal（Shell・Edit・View・Session・Help、画像のとおり）。

## 受け入れ（p001 で決めた）

1. zdesktop-terminal のメニューが浮いたタイトルバーと docked のシステムバーに出て、pointer・keyboard（F10）・shortcut で選ぶと
   terminal の action が動く（Venus: p003・p004 で済み。i915 実機: p005、未実施）。
2. 動的な更新（enabled・checked）が commit の単位で反映される。focus の窓のメニューがシステムバーに出る（Venus で済み）。
3. protocol の誤りは design.md §2.2 の error で返り、libzdesktop は送らずに errno で返す（Venus で済み、menu-probe）。
4. 規約の全文との照合（p005）。

設計と未決は [design.md](design.md)（§11 の未決 5 点、§12 の context menu の余地）。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws070-p001](phase001/phase.md) | 設計: protocol の定義（request・event・型・error）、libzdesktop の API、zdesktop の model・描画・入力・popup、試験。[design.md](design.md) | cleared | WS069-p008〜p010（2026-09-27 ユーザーがサブエージェントでの並行を指示） |
| [ws070-p002](phase002/phase.md) | protocol（libwayland の client 側と zdesktop の server 側）と zdesktop の menu model（transaction・更新）、menu-probe | cleared | p001 |
| [ws070-p003](phase003/phase.md) | zdesktop の描画と操作: 浮いたタイトルバーとシステムバーの項目、popup、keyboard、shortcut、activation | cleared | p002 |
| [ws070-p004](phase004/phase.md) | libzdesktop の API と zdesktop-terminal のメニュー（Shell・Edit・View・Session・Help） | cleared | p002、p003 |
| ws070-p005 | i915 実機、規約の全文との照合と回帰（最後） | planned | p001〜p004 |

## 試験の道具（plan/ws070/tests/）

| 道具 | 用途 |
| --- | --- |
| config-amd64-menu.mk、build-menu-image.sh | lean な guest image（guest harness から clang・lldb・libcxx を外した。BUILD は build/amd64） |
| menu-guest.sh | Venus guest（runtime は build/ws070-run） |
| menu-p002.sh | protocol の error と libzdesktop の検査（/bin/menu-probe） |
| menu-p003.sh | terminal の menu の全体（浮いた・docked・popup・submenu・keyboard・shortcut・動的更新・新しい窓・終了） |
| menu-occlude.sh | 隠れた題名の bar の項目は押せない |
| menu-regress.sh、zdesktop-p068-menu.sh | WS035 の zdesktop の回帰（p068 は題名の double click の位置を直した版） |
| style-compare.sh | 既存の file の style-check の件数を変更前と比べる |
