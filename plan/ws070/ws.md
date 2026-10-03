<!-- awesome-plan project=zedbsd record=ws070 -->

# WS070: zdesktop の System Menu と Titlebar Presentation（`xdg_toplevel_menu_v1`・`keiland_titlebar_v1`、libkeiland で包む）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし（p001〜p004 は 2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行、以後も同じ。main が merge）
Resume point: —（完了 2026-09-27。残りは下の「制限・移管」）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「この画像は、フローティングタイトルバーとドッキングタイトルバーにメニューを追加してみたものです。下記が仕様です。
X11サーバの実装が終わったら、OpenGLよりも、これを先に実装してもらえませんか？あとでGTK4やQt6のネイティブメニューバーとしても
利用可能にするつもりです。XDG拡張ではあるものの、libkeilandでラップします。」

Wayland のクライアントがメニューの意味（階層・ラベル・状態・action・shortcut・role・icon name）を zdesktop に渡し、zdesktop が
システムの UI として描き操作する。通常の窓では浮いたタイトルバーに、最大化（docked）ではシステムバーに同じメニューを出す。
仕様案は [spec.md](spec.md)、設計は [design.md](design.md)（§11 の 5 点は 2026-09-27 ユーザーが既定のまま確定: F10、shortcut は zdesktop が
実行、外の click は閉じるだけ、icon なし、ASCII の label。§12 の context menu）。

追加の目標（2026-09-27）: ユーザー「WS070に仕様追加します。WS071のサブエージェントでスケジューリングするのがいいと思います。」と
「zedBSD Titlebar Presentation Specification」（原文 [titlebar-spec.md](titlebar-spec.md)、設計 [titlebar-design.md](titlebar-design.md)）。
タイトルバーを system-owned な presentation surface とし、client は `MENU`・`CONTROLS`・`TABS` のどれかの model（意味）だけを渡す。
ユーザーの決定（§13-1）:「CONTROLS / TABS モードのウィンドウのアプリメニューの置き場所は、Aの推奨でお願いします。」→ 右端の「…」。

## 受け入れと結果（2026-09-27、completed）

| 受け入れ | 結果 |
| --- | --- |
| 1. terminal のメニューが浮いたタイトルバーと docked のシステムバーに出て、pointer・keyboard（F10）・shortcut で action が動く | Venus（p003・p004、menu-p003）と i915 実機（p005、capture の zdesktop-menu 11/11。WS075-p002 でも 11/11） |
| 2. 動的な更新（enabled・checked）が commit の単位で反映、focus の窓のメニューがシステムバーに | Venus（p003・p004） |
| 3. protocol の誤りは design §2.2 の error、libkeiland は送らずに errno | menu-probe（menu-p002）、titlebar-probe（titlebar-p008） |
| 4. 規約の全文との照合 | p005（WS071 と共有しない file）、p006・p012・p013（共有する file、`menu-shell.c` の段落の comment、`libkeiland/menu.c`・`menu-protocol.c`・`keiland.h` の手の照合） |
| Titlebar: CONTROLS | 配置と縮退（パンくずの前の段、検索が button に、priority の順に「…」へ）、button・segment・検索とパンくずの欄（zdesktop が持つ text field）・輪、docked の Application Zone、dock の animation の補間（p010）。最初の使い手 files の toolbar を移した（WS071-p014・p017） |
| Titlebar: TABS | strip、active・attention・×・＋、縮退（窓の題名が先に譲る → tab が縮む（題名は中央で切る）→ 矢印で scroll と wheel、「…」に隠れた tab）、mode の atomic な切替（p011）、白い bar でも見える地、Ctrl+Tab・Ctrl+Shift+Tab・Ctrl+PageUp/Down・Ctrl+W・Ctrl+T（p013） |
| Titlebar: glyph | glass の UTF-8 と動的 glyph cache（fallback font、日本語の題名と label）、role の icon（p009） |
| context menu | protocol version 2 の `get_context_menu`・`xdg_context_menu_v1`、libkeiland の `keiland_menu_popup`（WS071-p009 で実装） |

Titlebar の部分は Venus だけ。boot test は System Menu の p002〜p005 で PASS、titlebar の Phase では 2026-09-27 のユーザーの指示で行っていない。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws070-p001 | 設計: protocol、libkeiland の API、zdesktop の model・描画・入力・popup、試験 | cleared |
| ws070-p002 | protocol（libwayland と zdesktop）と menu model（transaction・更新）、menu-probe | cleared |
| ws070-p003 | 描画と操作: 浮いたタイトルバーとシステムバーの項目、popup、keyboard、shortcut | cleared |
| ws070-p004 | libkeiland の API と terminal のメニュー | cleared |
| ws070-p005 | i915 実機、規約の照合（WS071 と共有しない file）、回帰。libwayland の flush の EPIPE の不具合を直した | cleared |
| ws070-p007 | Titlebar Presentation の設計 | cleared |
| ws070-p008 | titlebar の protocol と model、libkeiland の `keiland_titlebar_*`、titlebar-probe | cleared |
| ws070-p009 | glass の UTF-8 と動的 glyph cache、role の icon | cleared |
| ws070-p010 | CONTROLS の presentation | cleared |
| ws070-p011 | TABS の presentation | cleared |
| ws070-p006 | 規約の照合の残り（WS071 と共有する file） | cleared |
| ws070-p012 | titlebar の規約の照合、回帰（締め） | cleared |
| ws070-p013 | （補強）TABS の見え方・切り方・配分・keyboard・wheel、menu の comment の手の照合 | cleared |

Phase の記録は git の履歴にある（WS の完了で削除した）。

## 制限・移管

- **Titlebar（CONTROLS・TABS）の i915 実機**: [WS075](../ws075/ws.md) の p002 で保留の files の capture の scenario（CONTROLS）と一緒に。
- Future Work（[future-work.md](../future-work.md)）: F-042（menu の item の icon、icon theme）、F-043（TABS の残り: drag での並べ替え、
  矢印の長押し、touch の大きさ、scroll する strip の 1 tab の幅）、F-045（GTK4・Qt6 の native menubar の backend）。
- 仕様の fallback（titlebar-spec §27、未対応の compositor での client 側の装飾）は files では持たない（ユーザーの決定）。
  他の client の fallback は、その client を作るときに決める。

## 試験の道具（plan/tools/titlebar へ移した）

- `build-menu-image.sh`・`config-amd64-menu.mk`（lean な guest image、probe を含む。WS035・WS071・WS074 の image の元）、`menu-guest.sh`
  （Venus の guest、runtime `build/ws070-run`）。
- `menu-p002.sh`（protocol の error と libkeiland、menu-probe）、`menu-p003.sh`（terminal の menu の全体）、`menu-occlude.sh`（隠れた題名の
  項目は押せない）、`menu-regress.sh OUTDIR TEST...`（WS035 の zdesktop の試験、`zdesktop-p068-menu.sh` は題名の double click の位置を
  直した版）、`menu-hw.sh`（i915 実機の System Menu、`flock /tmp/i915-hw.lock` の下で）。
- `titlebar-p008.sh`（protocol と model）・`p009`（UTF-8 と glyph）・`p010`（CONTROLS）・`p011`（TABS）・`p013`（tab の key・dock の途中・
  題名の配分・wheel）。`icons-host.c`（titlebar の icon を 4 つの大きさで描き、枠の中に収まるかを host で確かめる）、`style-compare.sh REV FILE...`（style-check の件数を前と比べる）。
- menu-* を files の guest で走らせるときは `GUEST_RUNTIME=build/ws071-run`。
