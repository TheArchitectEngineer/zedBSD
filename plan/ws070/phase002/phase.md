<!-- awesome-plan project=zedbsd record=ws070p002 -->

# ws070-p002: protocol（libwayland の client 側と zdesktop の server 側）と menu model

Phase ID: `ws070-p002`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）
承認: 2026-09-27 ユーザー「GLSLコンパイラとSystem Menu Extensionについて、サブエージェントで実装を進めてもらえますか。」
設計: [design.md](../design.md) §2、§3、§5

## 範囲

1. libwayland: `xdg_menu_manager_v1`・`xdg_menu_v1`・`xdg_toplevel_menu_v1` の表、typed の request の wrapper、
   `xdg_toplevel_menu_v1_listener` の typed dispatch、非公開の header。
2. zdesktop: 3 interface の server 側（global 7）、menu model（item の配列、transaction の写し、commit で差し替え）、
   error（object と code を名乗る `zwl_error_code`）、object の寿命（toplevel・表示先・menu・surface のどれが先に消えても）。

## 受け入れ

1. build warning 0、新しい file の style-check 0、既存の file は悪化させない。
2. 正しい model は error なく受け付けられ、誤りは design.md §2.2 の error（interface と code）で返る（Venus）。
3. transaction の commit ごとに model が差し替わる（log）。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/base/libwayland/menu-protocol.c`（新規）: 3 interface の `wl_message`・`wl_interface`、wrapper、`wlc_menu_dispatch`
  （`event.c` が interface 名で呼ぶ）。`xdg-toplevel-menu-v1-client-protocol.h`（新規、非公開、install しない）。`internal.h` が
  include し、`exports.map` の `xdg_*` が関数を出す。Makefile に source を足した。
- `userland/base/zdesktop/menu.c`（新規）: request の検査と model。item は 1024 個、深さ 8、文字列 255 byte まで。変更は
  transaction の中だけ（外は `not_updating`）。begin で深い写し、commit で差し替えて `generation` を進める。`remove_item` は子孫も。
  `zwl_menu_object_gone` が相互の pointer を外す（objects.c の `zwl_object_destroy` から）。log `ZWL MENU commit`・`set`・`activate`。
- `menu.h`（新規）、`zwl.h`（kind 3 つ、object に `menu_model`・`toplevel_menu`・`shown_menu`、`zwl_error_code`）、
  `protocol.c`（global と dispatch）、`objects.c`（hook）、`wire.c`（`zwl_error_code`）、Makefile。
- 試験の program `userland/base/tests/menu-probe`（新規、`platform/amd64/vmunix.mk` に link の規則）: 11 の server の case
  （transaction の外、ID 0、重複、submenu でない parent、checkbox でない checked、serial 違い、入れ子の begin、未知の role、同じ窓に
  2 つ目の表示先、正しい model、消した subtree の子）と libzdesktop の局所の検査の case。

### 最終の確認（2026-09-27、QEMU・Venus、lean image `plan/ws070/tests/build-menu-image.sh` → build/amd64/hdd-image.img。i915 実機は未実施）

- `plan/ws070/tests/menu-p002.sh` PASS（build/ws070-p002.log）: menu-probe の 11 の server の case と library の case が全部 ok
  （error は interface と code が合う。`ZWL ERROR client=N object=M code=C` が 10 行）。error の後も zdesktop は動き、後の terminal は
  menu を出した。
- `plan/ws070/tests/menu-p003.sh` PASS（build/ws070-p003.log、33 の確認が ok）。画面は build/ws070-p003/ の floating・edit・selected・
  keyboard・paste・submenu・view-large・docked-edit・about・two.png（自分で見て判定した: 浮いたタイトルバーの「Terminal  Shell Edit
  View Session Help」、docked のシステムバーの「zedBSD | T Terminal Shell Edit View Session Help | — ▢ ×」、無効の行は薄い、
  ✓・丸・›・shortcut、選択の青い帯）。1 回目の最終の run は新しい窓の Close の行の click が Shell の項目の上の press と扱われて
  FAIL した（pointer の移動が press より遅れて届いた。menu は Shell の再 press として正しく閉じた）。試験の click を 2 段の移動にして
  2 回続けて PASS。
- `plan/ws070/tests/menu-occlude.sh` PASS（build/ws070-occlude/）: 他の窓の本体に隠れた Shell の項目の press は menu を開かず、窓を
  前にすると開く。
- 回帰 `plan/ws070/tests/menu-regress.sh build/ws070-regress p059 p062 p063 p064 p065 p068 p069 p070 p071 p072`: 全部 PASS。
  p068 は WS070 版（`plan/ws070/tests/zdesktop-p068-menu.sh`）: WS035 の p068 は題名の bar の x+150 を double click するが、そこは
  今 terminal の Shell の項目なので menu が開く。題名（x+80）の double click にした版で PASS（WS035 の試験の直しは main の session）。
- boot test PASS（build/ws070-boot/login.png）。
- build warning 0（zdesktop、libwayland-client、libzdesktop、zdesktop-terminal、menu-probe）。style-check: 新しい file 0、変えた既存の
  file は変更前と同数（`plan/ws070/tests/style-compare.sh 24b12a47 …`）。
