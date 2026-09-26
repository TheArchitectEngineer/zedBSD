<!-- awesome-plan project=zedbsd record=ws071p003 -->

# ws071-p003: 一覧と移動（選択・keyboard・list 表示・並べ替え・Ctrl+L・矩形選択）

Phase ID: `ws071-p003`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

[design.md](../design.md) §3.2・§5.1・spec §4〜§6、§10〜§14、§35 の一部: 選択（click、Ctrl+click、Shift+click、矩形、keyboard）、
list 表示（列、見出しの click で並べ替え・逆順）、keyboard（矢印・Shift・Home/End・PageUp/Down・Enter・Esc・Backspace・Alt+←→・
Ctrl+↑↓・Ctrl+A・Ctrl+L・Ctrl+H・Ctrl+1/2・名前の先頭の入力で探す）、場所の入力欄（Ctrl+L、~ は home）、履歴に戻ったときの
cursor の復元、folder の読み直しで選択を保つ、status の pill（2 つ以上の選択の数と大きさ、短い message）。

## 受け入れ

1. 上の操作が host の試験の画面と Venus（QEMU）の log・画面で動く。
2. warning 0、新しい file と変えた file の `style-check.py` 0。

## 結果（2026-09-27）

cleared。

- 実装: `select.c`（選択の操作）、`ui-input.c`（pointer・矩形選択・keyboard・type-ahead・場所の入力欄・scroll の追従・選択の log
  `ZFILES SELECT`）、`ui-list.c`（list 表示、列の配置、見出し、日時の文 `Today 16:20`・`Yesterday`・`Sep 21 14:03`）、
  `ui-field.c`（1 行の text 欄: UTF-8・cursor・選択・US 配列の文字）、`ui.c`（入力の処理を ui-input.c へ分け、場所の入力欄の描画、
  `fm_ui_reload` が選択と cursor を名前で保つ、`fm_ui_message`）、`ui-grid.c`（list への切替、`fm_view_item_rect`、矩形、status の
  pill）、`dir.c`（owner の文）、`places.c`（タグの文）。
- 性能の手当て（受け入れではない。原因を調べて直した）: Venus で 1 frame に描画 65〜150 ms かかり、矩形の選択が画面で遅れた。
  角丸・縁・影の内側の行を距離の計算なしに一度に塗る（`canvas_inner_span`・`canvas_run`）ようにして描画は 20〜40 ms になった。
  残りの present（約 100 ms）は vkQueueSubmit・vkQueuePresentKHR の Venus の往復で、この program の外（並列の負荷の下の数値）。
  診断の log `ZFILES SLOW-FRAME draw= present= copy= acquire= queue= wait=`（250 ms を超えた frame だけ）を残した。
  遅れの確かめ方 `plan/ws071/tests/files-lag.sh`（矩形を段階的に引いて各段の画面を撮る）。
- 途中で直した不具合: `fm_field_insert` が 1 文字の挿入で buffer の外の byte を見て文字を捨てた。
- build: `make ... build/amd64/bin/zdesktop-files` warning 0、`style-check.py userland/base/zdesktop-files/*.c` 0、host の build 0。
- host の試験: `host-run.sh` で list.png（list 表示と Shift+↓ の範囲選択と pill）、band.png（矩形選択）、location.png（Ctrl+L の欄）、
  restore.png（戻ったときの cursor）、sorted.png を見た（build/ws071-host/）。
- **QEMU（Venus）**: `files-p003.sh` **PASS**（↓ と Shift+→ で 3 つ、Ctrl+click で 4 つ、click で 1 つ、list 表示、Size の見出しで
  並べ替えと逆順、Ctrl+L と path と Enter、list の空き地からの矩形で 2 つ、Backspace で戻る、Ctrl+↑ で home）。`files-p002.sh` も
  PASS（回帰）。画面 build/ws071-p003/keys.png・click.png・list.png・sorted.png・location.png・band.png を見た。
  guest の操作の道具 `plan/ws071/tests/qmp-input.py`（右 button と修飾 key を押したままの click）を足した。
- 実機（i915）: 未実施。
