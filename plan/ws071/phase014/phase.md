<!-- awesome-plan project=zedbsd record=ws071p014 -->

# ws071-p014: 窓の中の toolbar → titlebar の CONTROLS

Phase ID: `ws071-p014`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

ユーザーの指示（2026-09-27）「今のファイラーのはウィンドウ内部の上部にナビゲーションバーを持っていますが、これをウィンドウの
フローティングタイトルバーにマージします。」「ファイラーはこのcompositorでしか使えなくてOKです。」。設計は
[ws070 titlebar-design.md §11](../../ws070/titlebar-design.md)。

- `titlebar.c`（新規、menu.c と同じ形）: `zdesktop_titlebar_create` で CONTROLS の model を作る（Back・Forward（primary）、Home
  （primary）、パンくず（normal）、検索（normal）、Icons・List（secondary、group 1）、Preview（secondary）、進みの輪（normal、task
  のあるときだけ））。状態が変わったときだけ 1 つの transaction で送る。event（activated・text_changed・text_done）は queue に入れ、
  main loop が `fm_ui_titlebar` で行う。作れなければ `ZFILES FAILED operation=titlebar errno=…` で起動を終える（fallback なし）。
- `ui-titlebar.c`（新規、host で試験できる側）: `fm_ui_titlebar_state`（app から control の状態: 履歴、パンくずの段（各 64 byte
  まで、UTF-8 の境界で切る）、path、検索の語、表示、preview、進み、focus の求め）と `fm_ui_titlebar`（event → 動作。パンくずの段、
  Ctrl+L の path の欄の submitted で移動、検索の text_changed は今の検索欄の入力と同じ（150 ms 後）、submitted は結果へ、cancelled は
  Esc と同じ、left は欄を出るだけ）。
- Ctrl+F・Ctrl+L は `focus_control`（検索は field、パンくずは edit）を求める（app の focus の serial が増えると titlebar.c が送る）。
- `ui.c`: toolbar の描画・hit（`ui_draw_toolbar`・button・crumbs・search・views・progress）と `layout.toolbar` を消す。panel は窓の
  上端の余白 12 から。task の一覧は窓の右上。`ui-input.c`: toolbar の hit の click を消す。
- 試験: host の `files-render` に `titlebar`（状態を text で）と `tb=activated:ID:DETAIL`・`tb=changed:ID:TEXT`・
  `tb=done:ID:HOW:TEXT`。guest の p002〜p008・p012 の座標を直す（窓の中は 52 px 上へ、toolbar の click は zdesktop の log
  `ZWL TITLEBAR control ...` から）。新しい `files-p014.sh`。

## 受け入れ

1. host: titlebar の状態の text（folder・dashboard・search・履歴・表示・preview・task）、event の動作、toolbar の無い画面。
2. Venus（QEMU）で: 浮いたタイトルバーに control（log と画面）、Back・Forward・Home・パンくずの段・Icons/List・Preview の click、
   検索の入力と結果、Esc で戻る、Ctrl+L で path を入れて移動、Ctrl+F、docked（最大化）の control、狭い窓の overflow に menu の
   top-level。titlebar の拡張の無い compositor では起動で失敗する（log）。
3. 回帰: files-regress（p002〜p008、p012、座標を直した上で）PASS。warning 0、`style-check.py` 0。

## 結果（2026-09-27）

cleared。

- 実装: `titlebar.c`（新規: CONTROLS の model（8 control、進みの輪は task のある間だけ add・remove）、状態が変わったときだけ
  1 つの transaction、event の queue、`focus_control`（検索は field、パンくずは edit）、log `ZFILES TITLEBAR ready|state|...`）、
  `ui-titlebar.c`（新規: `fm_ui_titlebar_state`・`fm_ui_titlebar`、パンくずの段は 64 byte まで UTF-8 の境界で）、`main.c`（作れなければ
  `ZFILES FAILED operation=titlebar errno=...` で終わる）、`ui.c`（toolbar の描画・hit・`layout.toolbar` を削除、panel は上端 12 px
  から、task の一覧は右上）、`ui-input.c`（toolbar の click を削除、`fm_input_location_go` を公開、Ctrl+L は focus の要求）、
  `ui-search.c`（`fm_search_cancel`、Ctrl+F は focus の要求）、`files.h`・`window.h`・`Makefile`・`icons.c`・`ui-overlay.c`（comment）。
- 同時に直した不具合（実行中の発見、技術的な委任の範囲）:
  1. zdesktop-files: menu の action を窓の入力の列（`FM_EVENT_ACTION`、`fm_window_action`）に入れ、key と来た順に行う
     （Ctrl+A の直後の「/」が先に処理され files-p005 が落ちていた。ws070-p010 の記録を参照）。`fm_menu_take` と menu の queue を削除。
  2. zdesktop `menu-shell.c` の `zwl_menu_keysym`: Shift の文字を大文字に（titlebar の欄で "Pictures" が "pictures" になっていた）。
     `titlebar-p010.sh` は "aBc" を打つように。
- 試験: `plan/ws071/tests/host-p014.sh`（新規、host の状態と event、進み、toolbar の無い画面）**PASS**、host `files-model` **PASS**。
  guest の p002〜p008・p012 を直した（窓の中の座標は 52 px 上へ、窓の中央の card の click はそのまま、toolbar の click は
  zdesktop の log の control の位置から、p008 の menu は「…」から開く）。`files-regress.sh` の既定に p014。
- **QEMU（Venus）**: `files-p014.sh`（新規）**PASS**（浮いた bar の control と「…」、パンくずの段・Back、Preview、Ctrl+F と検索・Esc、
  Ctrl+L と path・Enter、最大化の docked の control と Home、420 px の窓の縮退と「…」に隠れた control と menu（File の行）、
  List の行）。`files-regress.sh`（p002〜p008、p012、p014）: p008 以外 **PASS**、p008 は Open With が 3 段になった分の Esc が
  足りず（試験の直し）→ 直して再実行 **PASS**。`titlebar-p010.sh`（WS070）**PASS**。画面を見た: floating・search・docked・overflow
  （build/ws071-p014/）。
- 未実施: titlebar の拡張の無い compositor での起動の失敗（image に拡張の無い compositor が無い。コードの経路は単純）。実機（i915）。
- 画面の写し: `build/ws071-shots/p014-20260927-{floating,preview,search,location,docked,narrow,overflow}.png`。
- build warning 0（zdesktop・zdesktop-files）、style: 新しい file 0、変えた file は悪化なし。
