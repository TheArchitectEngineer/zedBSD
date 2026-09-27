<!-- awesome-plan project=zedbsd record=ws035p078 -->

# ws035-p078: XKB keymap、key repeat、locked modifiers、wl_output v4

Phase ID: `ws035-p078`
Parent: [WS035](../ws.md)（p028 から 2026-09-27 に分割）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（N=3 のサブエージェントを常に走らせ、1 つが WS035 を続ける。デスクトップと graphics が最優先）

## 背景

zdesktop は `wl_keyboard.keymap` で `no_keymap`（/dev/null）を送り、key は evdev の code、repeat_info は rate 0 だった。
GTK・Qt・Chromium は libxkbcommon で keymap を compile して key を文字にするので、keymap が無いと文字を打てない。rate 0 は
「client は repeat しない」の意味で、toolkit は長押しで repeat しない（zdesktop-terminal は 0 を無視して自分の 400 ms・40 ms で
repeat していた）。wl_output は v2（name・description・release が無い）。

## 範囲

1. US の XKB keymap（include の無い完結した text、key code は evdev + 8、real modifier は標準の順で zdesktop の mask と一致）を
   起動時に file に書き、keyboard ごとに read-only の descriptor を `xkb_v1` で送る。作れなければ従来どおり no_keymap。
2. repeat_info: rate 25/s、delay 400 ms（zdesktop-terminal の既定と同じ）。
3. locked modifiers: Caps Lock（0x2）と Num Lock（0x10）を key の press で切り替え、`wl_keyboard.modifiers` の locked で送る。
4. wl_output v4: name（`ZDESKTOP-1`）、description（`zdesktop output WxH`）、release（v3）。
5. 試験: host で libxkbcommon による keymap の compile と keysym（`plan/ws035/tests/p078/run-host.sh`）、guest の probe
   （`userland/base/tests/seat-probe/`、`plan/ws035/tests/zdesktop-p078.sh`）。

## 判断が要る点

- repeat を rate 25・delay 400 ms にした（前は rate 0 = toolkit は repeat しない）。値はいつでも変えられる。
- wl_seat は v5 のまま（v6・v7 は touch と keymap の MAP_PRIVATE だけで、pointer・keyboard に新しい message が無い。v8 の
  axis_value120 は必要になったら）。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/base/zdesktop/keymap.c`・`keymap.h`（新規）: keymap の text（keycodes・types ONE_LEVEL/TWO_LEVEL/ALPHABETIC/KEYPAD・
  compat・symbols・modifier_map）、`zwl_keymap_open`（`/tmp/zdesktop-keymap-<pid>` に書いて O_RDONLY で開き直し、名前を消す）、
  `zwl_keymap_descriptor`（keyboard ごとの複製と大きさ）。
- `seat.c`: keymap を `xkb_v1` で（無ければ no_keymap）、repeat 25/400、modifiers の locked。`input.c`: Caps Lock・Num Lock の
  press で lock を切り替え、modifiers を送る。`main.c`: 起動時に keymap（`ZWL KEYMAP format=xkb_v1 errno=0`）。
- `protocol.c`: wl_output を v4 で広告、name・description、release（v3）。
- 試験: `plan/ws035/tests/p078/keymap-host.c`・`run-host.sh`（新規）、`userland/base/tests/seat-probe/`（新規）、
  `plan/ws035/tests/zdesktop-p078.sh`（新規）。lean image の config と vmunix.mk に probe。

### 確認

- host（libxkbcommon、`build/ws035-p078-host/`）: `run-host.sh` PASS。keymap が compile でき、Shift・Lock・Control・Mod1・Mod2・Mod4 の
  index が 0・1・2・3・4・6（zdesktop の mask 0x1・0x2・0x4・0x8・0x10・0x40）、a・1・Return・space・Escape・Up・F1・Super_L・
  Alt_L・KP_Home、Shift で A・exclam・question・ISO_Left_Tab、Caps Lock で A（1 は 1）、Num Lock で KP_7、各 modifier の key の
  press が正しい mask、file が text と NUL を持つ。
- QEMU・Venus（lean image、`build/ws035-p078/`）: `zdesktop-p078.sh` PASS。keymap は format 1・6903 byte・`xkb_keymap {` で始まり NUL で
  終わる（MAP_PRIVATE で読めた）、repeat 25/400、output v4 の geometry・mode（1280x800）・scale 1・name・description・done、Shift で
  depressed 1、Caps Lock で locked 2 と 0、release の後も error 無し。画面 `seat.png`。
- Num Lock は guest の USB keyboard の driver が key を出さない（HID usage 0x53 の表が無い）ので guest では未実施。
  [BUG-070](../../bugs/BUG-070.md) に登録（keypad・Print Screen・Scroll Lock・Pause・102nd・Menu も同じ）。host 試験で Num Lock の keymap は確認済み。
  追記（2026-09-27）: BUG-070 は WS073 p011 で直った。main（8a3914ce）を合わせた lean image（`build/ws035-p079-image.img`）で試験に
  Num Lock の段を戻し（locked 18、keypad の 7 が key 71、戻して 0）、`zdesktop-p078.sh` PASS（`build/ws035-p078-numlock/`）。
- 回帰（keyboard の repeat・modifiers は全 client に効くので広く）: p076 PASS、menu-regress の p059・p062〜p065・p068〜p072・p014
  PASS、menu-p002・p003 PASS、x11-p003・x11-p005 PASS（x11-p004 は lean image に glxtest が無く未実施）。boot test PASS
  （`build/ws035-p078-boot/login.png`、commit bd2495f5 の lean image）。
- i915 実機: 未実施。

### 規約

- style-check: 新しい file（keymap.c・keymap.h・seat-probe/main.c・keymap-host.c）0。変えた既存の file は変更前と同数
  （seat.c 3、input.c 2、main.c 8、protocol.c 5、zwl.h 0）。build warning 0。

### 制限

- keymap は US だけ（layout の選択は無い）。Compose・dead key は無い。
- locked modifiers は zdesktop の中だけで持つ（keyboard の LED は点けない）。
