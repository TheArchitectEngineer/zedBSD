<!-- awesome-plan project=zedbsd record=ws035p080 -->

# ws035-p080: xdg-decoration、cursor-shape、viewporter

Phase ID: `ws035-p080`
Parent: [WS035](../ws.md)（p028 から 2026-09-27 に分割）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（N=3 のサブエージェントを常に走らせ、1 つが WS035 を続ける。デスクトップと graphics が最優先）

## 範囲（2026-09-27 main の指示で縮めた）

1. `zxdg_decoration_manager_v1` v1: zdesktop は常に server_side と答える（glass の look の浮いたタイトルバーと menu を zdesktop が
   描くため）。作成と set_mode・unset_mode のたびに decoration の configure、窓が configure 済みなら窓の configure も。
2. `wp_cursor_shape_manager_v1` v1: pointer の client の shape を zdesktop が描く（I-beam、指、十字、左右・上下・斜めの resize、
   移動、禁止、砂時計。ほかの shape は矢印）。focus の変化・cursor の surface で矢印に戻す。
3. `wp_viewporter` v1: source と destination の状態（double-buffered、commit で適用、bad_value・viewport_exists・no_surface の error）。
   **この Phase で描くのは sub-surface の viewport だけ**（glass の look、source の uv と destination の大きさ、hit も）。窓の body・
   popup・plain の look の viewport の描画は shell.c・compose.c の変更が要り、WS070 の titlebar（ws070-p010）と衝突しないように
   [p081](../ws.md) に分けた（main の指示）。

## 受け入れ

1. Venus で `plan/ws035/tests/zdesktop-p080.sh` PASS（decoration の答え、sub-surface の viewport の画素、3 つの cursor shape の画素）。
2. 回帰: p076〜p079 と WS035 の zdesktop の試験（menu-regress）、menu-p002・p003、x11-p003・p005、boot test。
3. build warning 0、新しい file は style-check 0、既存の file は悪化させない。

## 結果（2026-09-27）

cleared。main（titlebar p008 と p079 の merge の解決）を合わせた tree（merge commit 3f0bd138）で確認。

### 実装

- `userland/base/zdesktop/decoration.c`・`cursor.c`・`viewport.c`・`extras.h`（新規）。protocol.c の globals に 10（decoration）・
  11（cursor-shape）・12（viewporter）、dispatch、surface の commit の最初で `zwl_viewport_commit`。objects.c で toplevel・decoration・
  pointer・surface・viewport の破棄を解く。seat.c の `zwl_cursor_default` と wl_pointer.set_cursor で shape を消す。shm.c の
  `zwl_arrow_destroy` で shape の image を消す（image は shape を初めて求められたときに作る）。compose.c の `compose_cursor` で shape の image を hotspot で描く
  （main の了承の小さな hunk）。subsurface.c の描画と hit を viewport の大きさと source に。
- cursor の image は白い図形と黒い縁で、図形は矩形・三角形・線・輪から作り、縁はその周りを求める（絵を source に持たない）。
- `userland/base/tests/extras-probe/`（新規。3 つの protocol の記述を probe 自身が持ち、libwayland の汎用の marshal と dispatch で話す
  （toolkit が wayland-scanner の code で話すのと同じ））、`plan/ws035/tests/zdesktop-p080.sh`（新規）、lean image の config と vmunix.mk。

### 確認（QEMU・Venus、lean image）

- merge の前の tree（9041a115 の zdesktop と probe を guest に置いた）: `zdesktop-p080.sh` PASS（`build/ws035-p080/`）。decoration は
  client_side を求めて server_side を 2 回受ける（作成と set_mode）、zdesktop のタイトルバーはそのまま（decoration.png）、sub-surface の
  viewport は赤の 4 分の 1 だけを 200x100 に（画素 3 点が赤、外が窓の色）、cursor は text（I-beam、pointer の点とその 6 px 下が白）、
  h で指、e で左右の矢印（矢印なら黒か透明の所が白）。画面 decoration.png・text.png・hand.png・resize.png。
- merge の後の tree の lean image（`build/ws035-p080-image.img`）: boot test PASS（`build/ws035-p080-boot/login.png`）。その guest で
  p077・p078・p079・p080・p076 PASS、menu-regress の p062〜p065・p068〜p070・p014 PASS、menu-p002・p003・x11-p003・x11-p005 PASS
  （x11-p004 は glxtest が無く未実施）。p059・p072 が FAIL（p071 は 1 回 FAIL、再実行で PASS）: zdesktop の起動が遅れ、試験の固定の
  `sleep 3` の間に socket ができず、先に起こした wltest が ENXIO で終わっていた。原因は cursor の shape の 10 の image を起動時に
  作っていたこと（Venus で遅い）。**shape の image を初めて求められたときに作るように直し**、その zdesktop で p059・p071・p072・
  p080 PASS（`build/ws035-p080m-rerun2/`、`build/ws035-p080-final/`）。
- i915 実機: 未実施。

### 規約

- style-check: 新しい file（decoration.c・cursor.c・viewport.c・extras.h・extras-probe/main.c）0。変えた既存の file は変更前と同数
  （seat.c 3、protocol.c 5、subsurface.c・shm.c・compose.c・objects.c・zwl.h 0）。build warning 0。

### 制限と移管

- 窓の body・popup・plain の look の viewport の描画と hit は [p081](../ws.md)（ws070-p010 の後）。
- decoration は client_side を認めない（zdesktop が常に描く）。client 側の装飾を認めるには shell.c で窓の枠を描かない道が要る。
- cursor-shape の tablet tool（get_tablet_tool_v2）は無い（EPROTO）。help・context_menu・copy・alias・zoom などの shape は矢印。
