<!-- awesome-plan project=zedbsd record=ws070p005 -->

# ws070-p005: i915 実機、規約の全文との照合（WS071 と共有しない file）と回帰

Phase ID: `ws070-p005`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（サブエージェントを最大 7 並列にして依存を守り WS を完了まで進める。main の session の伝達による要約）。
このサブエージェントは WS070 の締めと WS035 を担当

## 範囲

1. i915 実機（5330、VFIO、capture display）で zdesktop-terminal の System Menu: 浮いたタイトルバーと docked のシステムバーの
   menu、keyboard（F10・矢印・Enter・Esc）、menu の選択と shortcut が terminal に届くこと、Close Window。
2. WS070 で変えた全 source の、規約の全文（`plan/coding-style.md`）との照合。WS071（file manager と context menu の v2）が同時に
   変える file（`zdesktop/menu.c`・`menu-shell.c`・`menu.h`、`libzdesktop/menu.c`、`include/libc/zdesktop.h`、
   `libwayland/menu-protocol.c`・`xdg-toplevel-menu-v1-client-protocol.h`）は、merge の衝突を避けるため**指摘の記録だけ**にし、
   直しは p006（WS071 の merge の後）へ分けた（2026-09-27 main の session の指示「menu のコードの変更は最小に」による分割）。
3. 回帰: WS070 の試験（menu-p002・p003・occlude）、WS035 の zdesktop の試験（p059〜p072）、boot test。

## 受け入れ

1. i915 実機で 1 の操作が画面（capture）で確かめられる。
2. 共有しない file は規約の全文に合う（style-check 0、既存の file は悪化させない、手で照合）。共有する file の指摘は p006 に移す。
3. 回帰がすべて通る。build warning 0（我々の source）。

## 結果（2026-09-27）

cleared。共有の file の直しは p006 に移した（WS070 は incomplete のまま）。

### i915 実機（5330、VFIO、capture display。QEMU とは別の証拠）

`plan/ws070/tests/menu-hw.sh`（新規）→ `plan/ws031/tests/vkloop-hw.sh zdesktop` を `CAPTURE=zdesktop-menu ZDESKTOP_APP=home` で。
capture の scenario `zdesktop-menu` を `plan/ws031/tests/i915-capture.py` に足した（App Home から terminal、`ls /bin`、F10、→→、Enter、
Ctrl+0、題名の double click、F10、Esc、F10・↓・Enter。terminal の本体は背景色 1d2230 で探す）。

- 1 回目（review の直しの前、build/ws070-p005-hw1/）: 11 の check が全部 true（desktop_drawn、terminal_starts、terminal_found、
  menu_opens_floating、menu_moves_to_view、zoom_in_by_menu、shortcut_normal_size、docks、menu_opens_docked、menu_closes、
  close_by_menu）。guest の log（disk から）: `MENU open … item=1`→`item=2`→`item=3`（floating、y=361）、`activate item=30 action=6
  via=key` → `ZTERM ZOOM pixels=18`、`activate item=32 action=8 via=shortcut` → `ZTERM ZOOM pixels=16`、docked で `MENU open … item=1
  x=242 y=40`、`activate item=12 action=2 via=key` → `ZTERM DONE reason=menu-close`。
- 画面を自分で見た: menu-view（浮いたタイトルバーの「Terminal Shell Edit View Session Help」、View の popup に Zoom In（選択の青）・
  Zoom Out・Normal Size・Text Size ›・区切り・Fullscreen と shortcut の表示）、zoom-in（文字が大きい）、menu-docked（システムバーの
  「zedBSD | T Terminal Shell Edit View Session Help … — ▢ ×」、Shell の popup の New Window・Close Window と Ctrl+Shift+N・Q）、
  closed（terminal が消え、wl_shm の窓だけ）。
- 2 回目（直しの後、build/ws070-p005-hw2/）: 下の「2 回目」に記録。

### 規約の全文との照合

対象: WS070 の全 source（merge の commit `1765773c` と p004 の差分、基準 `24b12a47`）。機械の検査（`plan/tools/style-check.py`）は新しい
file 0、既存の file は変更前と同数（`plan/ws070/tests/style-compare.sh`）だった。その上で全文を手で照合した。

直した（この Phase、共有しない file）:

- `zdesktop-terminal/main.c`: `main_parse` の既定の段落に混ざっていた argv[0] の判定を段落に分けた。`main_loop` の選択の解除に
  段落の comment。`main_write_shell` の 3 節の条件を行に分けた。`main_menu_actions` の case の呼び出しに comment、log の前の空行。
  `main_new_window` の fork・close・display の判定を段落に分けた。`main_zoom` の段落。`main_start_paste` の loop の本体の if に
  comment（既定を上書きする形をやめ、if-else に）。
- `zdesktop-terminal/menu.c`: menu と window menu の作成を 1 つずつの段落に。`terminal_menu_refresh` の 2 つの判定を段落に。
  action の数（counter）の意味の comment。
- `zdesktop-terminal/screen.c`: `terminal_screen_text` の内側の loop に comment、空の cell の段落。
- `tests/menu-probe/main.c`: 3〜4 節の条件を行に分けた、呼び出しの中の呼び出し（`wl_display_get_error`）を変数に、成功の return を
  最後に分けた。
- `zdesktop/shell.c`（WS070 の hunk）: docked の題名・menu・button の段落を分けた。
- 形の決まりとして受け入れたもの: userland の `(void)parameter;`（userland に `UNUSED_PARAMETER` は無く、既存の file の形）、既定を
  代入してから if で上書きする 2 行（`ink = faint; if (focused) ink = dark;` の形。tree の多くの file の形）。

p006 に移した指摘（WS071 と共有する file。直しは WS071 の merge の後）:

- 3 節以上の条件が 1 行: `zdesktop/menu.c` 207・217・781、`zdesktop/menu-shell.c` 314・338・473・582・715・798・887・1387、
  `libzdesktop/menu.c` 870・897。
- `zdesktop/menu.c`: 2 つの確保をまとめて検査（`model_add` の label と icon_name、`items_copy`）。呼び出しを引数に入れ子
  （`model_edit` の `menu_word(…)`、`zwl_find(…, menu_word(…))`、`model_begin(menu, menu_word(…))`）。`error = …; return error;` の
  素通し（`model_request` の begin・commit・not_updating、`model_add` 等の `model_fail` の後）。`manager_request`・`model_request`・
  `place_request` の destroy の分岐が検査・呼び出し・return を 1 段落に詰めている。
- `zdesktop/menu-shell.c`: 長い段落の中の if と呼び出しの comment（`zwl_menu_draw_bar` の label と hit の記録 ほか）。
- `libzdesktop/menu.c`・`libwayland/menu-protocol.c`・`include/libc/zdesktop.h`: 同じ種類の照合を p006 で行う（この Phase では
  3 節の条件の検索だけ）。

### 照合で見つけた不具合（libwayland、この Phase で直した）

menu-p002 が 5 回に 1 回 `case=duplicate FAIL status=-1 code=0` になった（server は正しい error を log していた）。原因は
libwayland の client: compositor は protocol error を送った直後に接続を閉じる（`zdesktop/main.c` の「flush した fatal の接続は
すぐ閉じる」）。client の `wl_display_roundtrip` の flush（sync の request）がその後に当たると EPIPE で、`wlc_display_wait` が
読む前に失敗を返し、`wl_display_flush` が EPIPE を致命の error にしていたので、届いている error の event が読まれなかった。
upstream の libwayland は同じ理由で flush の EPIPE を無視して読みに行く。`userland/base/libwayland/client.c` を同じにした:
`wl_display_flush` は EPIPE を致命にしない（読みが end of file に当たったときに致命になる）、`wlc_display_wait` は EPIPE でも
poll して読みへ進み、POLLOUT は backpressure のときだけ待つ。直しの後、menu-p002 8 回と guest の中の menu-probe 60 回（1 回
12 接続）で失敗 0。Bug の登録は main の session に依頼する（Bug Board は main の所有）。

### 回帰（QEMU・Venus、lean image `plan/ws070/tests/build-menu-image.sh` → build/amd64/hdd-image.img）

- 直しの前（基準）: menu-p003 PASS（33 の確認）、menu-p002 PASS、menu-occlude PASS、WS035 の p059・p062〜p065・p068・p069・p071・p072
  PASS、p070 FAIL（lean の config に `zdesktop-x11server` が無かった: WS069 p008 で App Home の X の app は zdesktop-x11server で
  動くようになった。`config-amd64-menu.mk` に足した）。
- 直しの後: menu-p002 PASS（8 回）、menu-p003 PASS（3 回。1 回目は `ZTERM COPY` の log の確認が早すぎて FAIL: guest は copy していた
  （bytes=18）。`expect_log` を 5 秒まで待つようにした）、menu-occlude PASS、WS035 の p059・p062〜p065・p068〜p072 すべて PASS
  （p070 は zdesktop-x11server の Gears と zterm の画面を見た）。submenu.png（View > Text Size の submenu）を見た。
- boot test PASS（build/ws070-p005-boot/login.png を見た）。
- build warning 0（zdesktop、zdesktop-terminal、libwayland-client、libzdesktop、menu-probe、zdesktop-x11server）。style-check: 変えた
  file は変更前と同数（libwayland/client.c 23 = 変更前 23、他は 0）。`git diff --check` clean。

### 試験の道具の変更

- `plan/ws070/tests/menu-hw.sh`（新規）: i915 実機の run を lock の中で行い、/tmp の結果を OUTDIR に写す。
- `plan/ws031/tests/i915-capture.py`: scenario `zdesktop-menu`、`colour_box`・`region_difference`（WS031 の file。merge で注意）。
- `plan/ws070/tests/config-amd64-menu.mk`: `zdesktop-x11server`。
- `plan/ws070/tests/menu-p003.sh`: `expect_log` の待ち。

### 制限

- 実機の LCD の実表示は未実施（capture display の画像で判定。LCD は人が撮る）。
- 性能は測っていない（他のエージェントが machine を使っている）。
