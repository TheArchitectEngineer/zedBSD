<!-- awesome-plan project=zedbsd record=ws071p009 -->

# ws071-p009: context menu（WS070 の System Menu の protocol の version 2）

Phase ID: `ws071-p009`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）

## 範囲

[design.md](../design.md) §10.2（[ws070 design.md §12](../../ws070/design.md) の案を確定したもの）:

1. protocol: `xdg_menu_manager_v1` version 2 に `get_context_menu`（opcode 3: new_id、menu、surface、x、y、seat、serial）、新しい interface
   `xdg_context_menu_v1`（request `destroy`、event `activated(item, action, serial)`・`done()`）。
2. zdesktop: serial が最新の press のものか、surface が窓（toplevel）かを確かめ、違えば開かずに `done`（protocol error にしない）。menu の根の子を
   popup にして surface の点に出す（menu-shell.c の popup・submenu・keyboard・外の press で閉じる、を共有）。選ぶと `activated` の後に `done`、
   閉じると `done`。
3. libwayland（`menu-protocol.c`、非公開 header、event の dispatch）と libzdesktop（`zdesktop_menu_popup`・`zdesktop_context_menu_destroy`、
   `ZDESKTOP_VERSION` 6、manager は zdesktop が持てば version 2 で bind）。
4. zdesktop-files: 右 press で項目を選び（選択の一部ならそのまま）、何の上かで context menu を作って出す（`ui-context.c`、host で試せる）:
   項目（Open、Open in New Tab（folder）、Open With ▸、Cut、Copy、Paste、Rename、Duplicate、Tags ▸（checkbox）、Get Info、Move to Trash）、
   Trash の項目（Put Back、Delete Immediately、Empty Trash、Get Info）、空き地（Empty Trash（Trash）、New Folder・Paste（folder）、View ▸、
   Sort By ▸、Show Hidden Files）、sidebar の場所（Open in New Tab、Remove from Sidebar（Favorites の folder））。行の ID は action から
   （1000 + action、どの menu でも同じ）、submenu は 100〜103、線は順番。新しい action: Open in New Tab、Put Back、Delete Immediately、
   Empty Trash、場所の Open in New Tab・Remove from Sidebar。

設計から外したもの（戻せる）: Open in New Window・Move To ▸・Share（design の案）は出さない（New Window は menubar、Move To と Share は
後で）。error `bad_surface` は作らず、窓でない surface は `done` だけ（古い serial と同じ扱い）。

## 実装（2026-09-27）

- libwayland: `menu-protocol.c`（manager version 2、`get_context_menu`、`xdg_context_menu_v1` の表・wrapper・`wlc_context_menu_dispatch`）、
  `xdg-toplevel-menu-v1-client-protocol.h`、`internal.h`、`event.c`。
- zdesktop: `menu.c`（`context_create`、`zwl_menu_send_context_activated`・`_done`、`ZWL_CONTEXT_MENU` の request と退場）、`menu-shell.c`
  （`shell_menu.context`、`zwl_menu_open_context`、context の model と place、`shell_place`、選択・閉じるの `done`、`zwl_menu_forget`、
  tick で根の popup を消さない、左右の key で bar を渡らない）、`menu.h`、`zwl.h`（kind）、`protocol.c`（global version 2、dispatch）、
  `objects.c`。
- libzdesktop: `menu.c`、`include/libc/zdesktop.h`、`exports.map`。
- zdesktop-files: 新 `ui-context.c`、`ui-input.c`（`input_context`）、`ui-menu.c`、`menu.c`（`fm_menu_context`、model を作り直す、`done` の後の
  片付けは次の refresh で）、`main.c`（`FM_REQUEST_CONTEXT`）、`files.h`、`window.h`、`Makefile`。
- 試験: `plan/ws071/tests/host-p009.sh`（新）、`host-render.c` の `context`、`files-p009.sh`（新）。

## 検証（amd64 だけ、2026-09-27）

- host: `host-build.sh`（-Werror）通る。`host-p009.sh` 全部 ok（項目・空き地・Trash・sidebar の場所の行と enabled・checked、場所の Open in New
  Tab）。`host-p013.sh`・`host-p014.sh`・`files-model` PASS。
- guest（QEMU、Venus、lean image、warning 0）: `files-p009.sh` PASS（Budget.csv の右 click で zdesktop が点に menu を開く、Get Info が
  action 4 で戻り情報が開き done、空き地の View ▸ as List が submenu から action 16、Downloads の Open in New Tab、Esc で done、ERROR 無し）。
  画面 `build/ws071-p009/{items,empty}.png`。1 回目は zdesktop の tick が根の popup（parent = 0）を「submenu が消えた」と見て閉じていた →
  直した。
- 回帰（同じ image）: menu-regress（p059 p062 p063 p064 p065 p068 p069 p070 p071 p072 p014）・menu-p002・menu-p003・titlebar-p008・
  titlebar-p009・titlebar-p010 PASS、files-regress（p002〜p008・p012・p013・p014・p015）PASS、boot test PASS（`build/ws071-p009-boot/login.png`）。
- 規約: 新しい `ui-context.c` の style-check 0。変えた file は悪化なし（`host-render.c` は 56 → 57: 既存の action の `strcmp` の連鎖に `context`）。
- 実機（i915）: 未実施。

画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws071-shots/p009-20260927-venus-{context-items,context-empty-view}.png`。
