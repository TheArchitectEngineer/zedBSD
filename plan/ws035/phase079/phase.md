<!-- awesome-plan project=zedbsd record=ws035p079 -->

# ws035-p079: client 間の clipboard（wl_data_device_manager）と terminal の Copy・Paste

Phase ID: `ws035-p079`
Parent: [WS035](../ws.md)（p028 から 2026-09-27 に分割）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（N=3 のサブエージェントを常に走らせ、1 つが WS035 を続ける。デスクトップと graphics が最優先）

## 背景

zdesktop には client 間の clipboard が無く、terminal は自分だけの clipboard を持っていた。GTK・Qt・Chromium の Copy・Paste は
core の `wl_data_device_manager` を使う。libwayland（zedBSD の client library）も data の interface を持っていなかった。

## 範囲

1. zdesktop: global `wl_data_device_manager` v3、`wl_data_source`（offer・destroy・set_actions）、`wl_data_device`（set_selection・
   start_drag・release）、`wl_data_offer`（receive・destroy・accept・finish・set_actions）。selection は keyboard の client に
   （新しい offer（server の ID の範囲で作る）と type、selection の event）、focus を得る client には keyboard の enter の前に。
   receive は source の client へ send（fd を渡す）。新しい selection は前の source を cancelled に、source の破棄は clipboard を空に。
   drag and drop は「最小」: start_drag は source をすぐ cancelled にする。
2. server が作る object の ID（0xff000000 から）と、その破棄で delete_id を送らないこと。
3. libwayland: 4 つの interface（記述・wrapper・listener の型・header）。event は汎用の dispatch（p075）で listener へ。
4. terminal: Edit > Copy は selection を出し（UTF-8 と plain text）、Edit > Paste は他の client の selection を pipe で受ける
   （自分の selection のときは手元の文字列をそのまま。自分に書かせて自分で読むと待ち合う）。menu の Paste の有効も selection から。
5. 試験: `userland/base/tests/data-probe/`、`plan/ws035/tests/zdesktop-p079.sh`。

## 判断が要る点

- drag and drop は offer しない（start_drag の source はすぐ cancelled）。toolkit は drag を始めても何も起きない。DnD は必要になったら
  別 Phase（可逆）。
- set_selection の serial は検査しない（keyboard の client なら誰でも設定できる）。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/desktop/wayland/data.c`・`data.h`（新規）: 上の 1。`protocol.c` の globals の 9 と dispatch、`seat.c` の focus の変化で
  keyboard の enter の前に `zwl_data_focus`、`objects.c` の source の破棄で `zwl_data_object_gone`、`zwl_create_server`
  （server の範囲の ID、使用中を避けて巡回）と server の ID の破棄で delete_id を送らない。`zwl.h` に kind・field。
- `userland/desktop/libwayland/data-device-protocol.c`（新規）と `wayland-client-protocol.h`（upstream の名前・opcode・`_SINCE_VERSION`・
  dnd_action）。`API-PROVENANCE.md` に追記。
- `userland/desktop/terminal/clipboard.c`（新規）、`window.c`（manager の bind、device、key の serial、close）、`main.c`
  （Copy・Paste・menu の状態）、`terminal.h`。
- `userland/base/tests/data-probe/`（新規）、lean image の config と vmunix.mk。

### 確認（QEMU・Venus、lean image、runtime `build/ws035-run`）

- `plan/ws035/tests/zdesktop-p079.sh` PASS（`build/ws035-p079/`）: a の selection（2 つの type）を後から起きた b が focus で受け、
  pipe で `hello from a`（12 byte）、a の source の破棄で clipboard が空（b は focus を得たとき `selection none`）、b の selection を a が受け
  （`b says hi`）、a の新しい selection で b の source が cancelled、terminal の Copy（Select All の画面 68 byte）を probe c が受け、c の
  `touch /tmp/p079-pasted` と改行を terminal の Paste が受けて shell が実行（file ができた）。画面 two.png・terminal-copy.png・
  terminal-paste.png。
- 回帰: p076 PASS、menu-regress の p059・p062〜p065・p068〜p072・p014 PASS、menu-p002・p003 PASS（terminal の menu の Paste の
  有効を含む）、x11-p003・x11-p005 PASS（x11-p004 は lean image に glxtest が無く未実施）、p075 の host 試験 PASS。boot test PASS
  （`build/ws035-p079-boot/login.png`、commit 223642f1 と main（8a3914ce）の lean image）。その image の guest でも p079 PASS。
- i915 実機: 未実施。

### 規約

- style-check: 新しい file（data.c・data.h・data-device-protocol.c・clipboard.c・data-probe/main.c）0。変えた既存の file は変更前と同数
  （seat.c 3、protocol.c 5、objects.c 0、zwl.h 0、wayland-client-protocol.h 23、terminal の main.c・window.c・terminal.h 0）。build warning 0。

### 制限

- drag and drop・primary selection（中 click の貼り付け）は無い。
- source の send は client が書くまで待たない（zdesktop は fd を渡すだけ）。大きな text の転送の途中で source が止まれば受け手は時間切れ
  （terminal は 2 秒）。
