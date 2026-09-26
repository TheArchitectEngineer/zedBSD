<!-- awesome-plan project=zedbsd record=ws070p004 -->

# ws070-p004: libzdesktop の API と zdesktop-terminal の menu

Phase ID: `ws070-p004`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）
承認: 2026-09-27 ユーザー「GLSLコンパイラとSystem Menu Extensionについて、サブエージェントで実装を進めてもらえますか。」
設計: [design.md](../design.md) §4、§7

## 範囲

1. libzdesktop（`include/libc/zdesktop.h`、`userland/base/libzdesktop/menu.c`）: service・menu・window menu の API、ID の鏡による
   局所の検査、global の発見を自分の queue で、表示先の event を窓の queue で。
2. zdesktop-terminal: Shell・Edit・View・Session・Help と、その動作（新しい窓、閉じる、選択と terminal 内の clipboard、zoom と
   Text Size、fullscreen、^C・^D、画面の消去・初期化、About）。状態（Copy・Paste の enabled、Zoom の限界、radio、Fullscreen）を
   1 transaction で更新。

## 受け入れ

1. client は protocol の header を include せず libzdesktop だけで menu を出す（zdesktop-terminal は `<zdesktop.h>` だけ）。
2. zdesktop-terminal の menu が浮いたタイトルバーと docked のシステムバーに出て、選ぶと terminal の動作が起き、動的な更新が commit の
   単位で反映される（Venus）。
3. libzdesktop は protocol error になる呼び出しを送らずに errno で返す。

## 結果（2026-09-27）

cleared。

### 実装

- libzdesktop: `zdesktop_menu_service_open`（無ければ NULL・ENOTSUP、toolkit は client 側の menu へ）、`zdesktop_menu_*`（create、
  destroy、begin、commit、append、insert、remove、set_label・action・enabled・visible・checked・role・icon_name・shortcut）、
  `zdesktop_window_menu_*`（create、set、destroy、listener: activated・opened・closed）。`ZDESKTOP_VERSION` 2。libzdesktop.so は
  libwayland-client.so に link（`platform/amd64/vmunix.mk`、`exports.map`、Makefile の依存）。
- zdesktop-terminal: `menu.c`（新規、28 item の表）、`main.c`（action の実行、clipboard と paste、zoom、新しい窓は二重 fork で
  `--token=<run>-new`）、`window.c`（configure の fullscreen の状態、`terminal_window_type`・`set_fullscreen`、menu の後片付け）、
  `font.c`（`terminal_font_resize`、測る処理を `font_measure` に分けた）、`screen.c`（`terminal_screen_text`）、`render.c`（選択の色）、
  `terminal.h`。
- 正直な動作として記録: clipboard は terminal の中だけ（zdesktop に wl_data_device が無い）、選択は Select All の画面全体だけ
  （pointer の範囲選択は無い）、About は画面に 2 行を書く（dialog が無い）、Clear Screen は画面だけを消す。

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
