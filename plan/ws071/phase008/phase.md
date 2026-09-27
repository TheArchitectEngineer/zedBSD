<!-- awesome-plan project=zedbsd record=ws071p008 -->

# ws071-p008: menubar（System Menu）・New Window・shortcut・Help

Phase ID: `ws071-p008`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

2026-09-27 に元の p008 からタブを p013 へ分けた。[design.md](../design.md) §10.1、spec §29、§35〜§37:

- `menu.c`（新規、Wayland 側）: libzdesktop の `zdesktop_menu_*` で File・Edit・View・Go・Window・Help（design §10.1 の表から
  タブの項目を除く）。修飾 key の付いた shortcut だけ登録。Open With（8 枠）・Tags（16 枠）・List Columns（6）は枠を作って
  状態で名前・見える・check を変える。選択・clipboard・undo・履歴・表示に合わせて enabled・checked を 1 transaction で。
- `ui-menu.c`（新規、host で試せる側）: `fm_ui_action`（menu の action を実行）と `fm_ui_menu_state`（状態の計算。Open With は
  選択の file が変わったときだけ読む）。text の欄に focus があれば Cut・Copy・Paste・Undo・Redo を無効にし（zdesktop が key を
  取らず欄に届く）、Select All は欄の全選択。
- `ui-help.c`（新規）: Help の 3 つの card（File Manager Help、Keyboard Shortcuts、About Files）。
- New Window（Ctrl+N、自分を新しい process で今の folder に、token は `-new`）、Close Window（Ctrl+Shift+W）、Minimize（Ctrl+M）、
  Zoom（`xdg_toplevel` の maximize）。menu の無い compositor（と host の試験）でも同じ key が効くよう `ui-input.c` に表。

## 受け入れ

1. host: `action=N`・`state` で action と状態、Help の card の画面。
2. Venus（QEMU）: 浮いたタイトルバーに menu、pointer で File・View・Open With・Tags・Help、shortcut（via=shortcut）で Get Info・
   Desktop・Back、New Window と Close Window、Minimize。画面を撮る。
3. warning 0、`style-check.py` 0、回帰（p002〜p007、p012）PASS。

## 結果（2026-09-27）

cleared。

- 実装: `menu.c`（新規: 76 の固定の項目と Open With 8・Tags 16・List Columns 6 の枠、shortcut、役割、状態の transaction、
  activation の queue）、`ui-menu.c`（新規: `fm_ui_action`、`fm_ui_menu_state`、Open With の cache）、`ui-help.c`（新規: 3 つの card）、
  `main.c`（menu の open・close、action の実行、要求（New Window・Minimize・Zoom・Close）、menu の状態は入力の後と 250 ms ごと、
  New Window は自分を `--token=…-new` と今の folder で `fm_apps_spawn`）、`window.c`（`fm_window_minimize`・`fm_window_zoom`）、
  `apps.c`（`fm_apps_spawn`: fork 2 段、stdin・stdout・stderr は残し他の descriptor は閉じる）、`ui-input.c`（`fm_input_sort_by`・
  `fm_input_open_selection`・`fm_input_enclosing`・`fm_input_location` を公開、menu の key の表、Help の card の key と click）、
  `ui.c`（Help の card の描画）、`window.h`・`files.h`・`Makefile`。design.md §10.1 に実装の注記。
- 試験: `host-render.c` に `action=N` と `state`。`files-p008.sh`（新規）。`files-regress.sh` の既定に p012・p008。
- host: action（List、Copy、タグ、Sort、New Window の要求、Help の 3 つ、Desktop・Trash・Back）と状態（選択・paste・undo・
  back・forward・openers・tags の check）を確かめた。画面 build/ws071-host/p008-shortcuts.png・p008-guide.png・p008-about.png を見た。
- **QEMU（Venus）**: `files-p008.sh` **PASS**（浮いたタイトルバーの File Edit View Go Window Help、pointer で File・View > List・
  File > Open With（Record・Terminal (less)・Terminal (ed)）・Edit > Tags > Work・Help > Keyboard Shortcuts、shortcut（via=shortcut）で
  Get Info・Go > Desktop・Back、Ctrl+N の 2 つ目の窓（f1-new、ZWL MAP client=2）とその Ctrl+Shift+W、Ctrl+M の最小化）。画面
  build/ws071-p008/floating.png・file-menu.png・view-menu.png・open-with.png・tags-menu.png・help-shortcuts.png・two-windows.png・
  minimized.png を見た。途中で直した試験の不具合: activation の log の grep の形、2 つ目の process の終わりを待たずに数えていた
  （待つ loop に）。
- 回帰 `files-regress.sh`（p002〜p007、p012）**PASS**（build/ws071-p008-reg/、p008 はこの回の後に上の直しで再実行して PASS）。
- build warning 0（zdesktop-files。image の build で出た warning は merge で再 build された openssh・openssl の既存のもの）、
  host の build 0、`style-check.py` 0。
- 実機（i915）: 未実施。
- 制限: text の欄の Cut・Copy・Paste・Undo は欄に clipboard が無いので何もしない（menu は無効にして key を欄に渡す）。表示の設定
  （icon/list、列）は保存しない（新しい窓は既定）。タブの項目は p013。
- 2026-09-27 ユーザーの指示: toolbar（戻る・進む・パンくず・検索・表示の切替）は WS070 の titlebar 仕様（CONTROLS）で浮いた
  タイトルバーへ移し、窓の中の bar は無くす（fallback は持たない。zdesktop 専用でよい）。ws070-p007 の設計と後の Phase で扱う。
