<!-- awesome-plan project=zedbsd record=ws074p056 -->

# ws074-p056: 部品化 3 — DOM の形の入力と engine の既定の動作

Phase ID: `ws074-p056`（2026-09-28 main が割り当て、[p053](../phase053/phase.md) の手順 3）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p055

## 範囲

入力を DOM の形（key・code・text、pointer・wheel・focus）で view に入れ、既定の動作（scroll の key、link の activation、Tab の
focus の移動、form の欄への文字の入力（あれば）、戻る・進むの key）を engine が行う。shell は evdev をこの event に変えるだけ。
ZBROWSER の診断の行は変えない。

## 設計

- `view/view.h`（部品の API、draft [browser_view.h](../phase053/browser_view.h) の入力の部分）:
  `browser_view_pointer_move`・`_pointer_button`（DOM の button 番号 `BROWSER_BUTTON_*`、押しと離し）・`_pointer_leave`・`_wheel`
  （x・y の距離、pixel）・`_key`（DOM の key・code・text、押す・離す・repeat）・`_focus`（呼ぶ側の program の focus）。修飾は
  `BROWSER_MOD_SHIFT`・`_CTRL`・`_ALT`・`_META`。p054 の `browser_view_click`・`_scroll_by`・`_scroll_pages`・`_scroll_to` と
  `enum browser_scroll_place` は公開の API から外した（view の中の static に）。`_scroll_y` は残す（FRAME の行）。
- engine の既定の動作（`view/view.c`、page の scripts が取り消さなければ）:
  - 押しと離しが同じ button で 4 pixel 以内なら click（主 button: click の event、取り消されなければ下の link を `link` の callback に
    聞いて辿る）。主 button の押しは下の focus できる要素に focus を移す（ring なし）、無ければ focus を外す。戻る・進むの button
    （DOM の 3・4）は mouseup で履歴を 1 つ動く。
  - wheel: 下の要素に `wheel`、取り消されなければ縦の距離だけ scroll。
  - key: focus の要素（無ければ body）に `keydown`・`keyup`、文字を打つ key（Ctrl・Alt・Meta なし）には `keypress` も。keydown が
    取り消されなければ: ↑↓ 40 px、PageUp・PageDown（view の高さ − 40）、Space（Shift で上）、Home・End、Tab・Shift+Tab（順次の
    focus、ring 付き、見える所まで scroll）、Enter（focus の link か button に click、取り消されなければ link を辿る）、Alt+←・Alt+→・
    BrowserBack・BrowserForward（履歴）、F5・Ctrl+R・BrowserRefresh（再読み込み）、Escape・BrowserStop（読み込みの中止）。
  - 順次の focus の順: tabindex が 1 以上の要素をその値の順、次に tabindex 0 と focus できる要素（href のある a・area、disabled でない
    button・select・textarea・hidden でない input）を文書の順。box の無い要素は飛ばす。最後の後は何も focus しない（次の Tab で最初へ）。
  - focus の ring: keyboard で動かした focus だけ、view が program の focus を持つ間、要素の box の外側 1 px に 2 px の枠
    （0xFF1A73E8）を display list の最後に足す（`paint_add_ring`、CPU と GPU の描画が同じ）。focus の変化は layout をやり直さず
    display list だけ作り直す（`page_needs_paint`）。
  - **form の欄への文字の入力: 無い**（form の control の描画と入力は p032）。text は keypress の key として page に届くだけ。
- binding（`bind/input.c` 新）: `KeyboardEvent`（key・code・repeat・location・isComposing・keyCode・which・修飾）、`WheelEvent`
  （deltaX・deltaY・deltaZ・deltaMode、MouseEvent の子）、`FocusEvent`（relatedTarget は null）と構築子。MouseEvent に shiftKey・
  ctrlKey・altKey・metaKey。focus・blur（bubble しない）と focusin・focusout。`bind_event_prepare`（event.c の `event_fire` を分けた）。
- page（`page/input.c` 新）: `page_mouse_event`・`_wheel_event`・`_key_event`・`_focus_at`・`_focus_move`・`_window_focus`・
  `_activate_focused`・`_focus_rect`・`_needs_paint`・`_paint_focus`。focus の要素は heap の root（`page->focused`）、文書から外れた
  要素は focus を失う。p030 の `page_click` と要素の hit test は script.c から input.c へ。
- layout（`layout/bounds.c` 新）: `layout_node_bounds`（要素の block の border box と行の fragment の和）。
- shell: `shell/keys.c`（新、evdev → DOM の code・key・text、US 配列、修飾と button の変換）、`window.c` は押し・離し・repeat・
  pointer の移動（続く移動は 1 つにまとめる）・離れる・横の wheel・keyboard の focus を queue に。`shell.c` は shell 自身の
  shortcut（Ctrl+Q・Ctrl+W で閉じる、Ctrl+L で場所の編集。page には届かない）以外を view に渡す。
- 診断: 既存の ZBROWSER の行はそのまま（FRAME・LINK・NAVIGATE・LOADING・STOPPED・CONSOLE・ERROR click）。入力の失敗の行
  `ZBROWSER ERROR input error=`・`ZBROWSER ERROR key error=` を足した（今までは key の失敗を報告する場所が無かった）。

## 確認（host は Debian の cc と lavapipe、guest は QEMU の Venus）

- host の build（-Werror、plain と ASan）warning 0。amd64 の image の build（`build-browser-image.sh`、browser の warning 0）。
  style-check（変えた 18 file）0。
- 変更の前後で試験の page 7 つと画像の page 3 つの `--render`・`--render-gpu`・`--run`・4 種の dump と stderr の 126 file が byte で
  同じ（`build/p056/compare.sh`）。golden の dump 32/32（keys.html の 4 つを足した）。`run-http-tests.py` 同期 14/14、`--async` 16/16、
  ASan の `--async` 16/16、`run-loader-tests.py` 11/11。
- 新しい host の試験 `plan/ws074/tests/host-view.c`（view の入力の API を直接、`pages/keys.html` の listener の console で確かめる）:
  59/59（plain と ASan）。key の名前と keyCode、keypress、repeat、Tab の順（tabindex 1 → 2 → 文書の順）と ring、見える所への scroll、
  最後の後の focus 無し、Shift+Tab、Enter の click と link、scroll の key と取り消された keydown、wheel と取り消された wheel、click の
  mousedown・mouseup・click と pointer の focus（ring なし）、drag は click でない、program の focus の出入り、Alt+←・Alt+→・
  BrowserBack・forward の button・F5・Ctrl+R、修飾なしの ← と Escape は何もしない。
- guest（Venus）: 新しい `browser-p056.sh` status 0（1 回目で）: 文字の keydown・keypress・keyup、Tab の focusin（three・two・one・
  four・last）と ring、最後の link が見える所へ scroll（scroll=1250）、Enter で LINK・NAVIGATE、Alt+← で戻る、↓ 40 px、page が
  取り消す Shift+↓ で scroll しない、wheel の event、link の click（mousedown・click・LINK・NAVIGATE）、Ctrl+Q で閉じる、ERROR 行なし。
  既存の窓の試験 `browser-p045.sh`（titlebar・link・履歴・場所の欄・F5）・`p014`（End・wheel・Home の scroll、GPU と CPU の比較、
  Ctrl+Q）・`p030`（scripts の click）・`p050`（Esc の STOPPED、guest の HTTP 16/16）・`p017`（https）・`p021`（画像）・`p052`
  （背景画像）すべて status 0（1 回目で、同じ guest で続けて）。
- 写真: `/home/awe/zedBSD-rpi4/build/ws074-shots/p056-20260928-focus-ring.png`（Tab の ring）・`p056-20260928-focus-scrolled.png`
  （見える所へ scroll した最後の link の ring）。実機は未実施。

## 残り・移管

- form の control（input・textarea）の描画と文字の入力、Space・Enter での button の既定の動作の残り: p032。
- `document.activeElement`・`element.focus()`・`blur()`、`mousemove`・`mouseover`・`mouseout`・`contextmenu`・`dblclick`、
  FocusEvent の relatedTarget、KeyboardEvent の location の左右: p031（event と入力）。
- 横の scroll（wheel の deltaX・←→）、overflow の要素の中の scroll: 後の layout の Phase。
- 入力の方法（IME、composition）: 未計画。
