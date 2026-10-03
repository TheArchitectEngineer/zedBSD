<!-- awesome-plan project=zedbsd record=ws035p014 -->

# ws035-p014: ウィンドウ一覧のタイル表示（Windows+Tab）

Phase ID: `ws035-p014`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（サブエージェントで WS を完了まで進める。main の session の伝達による要約）

## 範囲（2026-09-27 に再定義）

タイル表示そのものは p063 の Wiseview（下端からの drag、グリッドのタイル、選択・閉じる・drag）で作られた。この Phase は残りの
**Windows+Tab**、つまり keyboard からの Wiseview にする:

1. Super+Tab で Wiseview を開く（最前面の窓が現在のタイル）。開いている間の key は Wiseview のもの（client に届かない）。
2. Tab・→・↓ で次、Shift+Tab・←・↑ で前のタイル（端で回る）。現在のタイルは縁の光で示す（p063 の描画）。
3. Enter・Space で選ぶ（最小化の窓も戻り、最前面になり、Wiseview が閉じる）。Esc・Super+Tab で閉じる。

## 受け入れ

1. Venus で 1〜3 が log と画面で確かめられる。閉じた後の key は Wiseview に取られない。
2. WS035 の zdesktop の回帰（p059・p062〜p072）と WS070 の menu の試験が通る。boot test。build warning 0、style-check。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/desktop/wayland/shell.c`: `zwl_glass_key` の先頭で、Wiseview が開いている（開きつつある）間は `wiseview_key` がすべての
  key を取る。Super+Tab で `wiseview_open_key`。新しい static 関数 `wiseview_showing`・`wiseview_open_key`・`wiseview_key`・
  `wiseview_close_key`（log `ZWL WISEVIEW opening key`・`current surface=N`・`select surface=N via=key`・`close key`）。
  `MODIFIER_SUPER`（0x40）。
- **Super key が入力の道に無かった**（見つけて直した）: USB HID の keyboard（`src/drivers/usb/usb-hid.c`）は modifier の usage
  0xe3・0xe7（左右の GUI）を訳さず、boot keyboard の modifier の field にも入れていなかった。PS/2（`src/drivers/platform/pcat/ps2-8042.c`）
  も E0 5B・5C を訳していなかった。`include/uapi/input.h` に Linux の evdev と同じ値の `KEY_LEFTMETA`（125）・`KEY_RIGHTMETA`（126）、
  USB HID に 0xe3・0xe7、PS/2 に `leftmeta`・`rightmeta`、`src/drivers/generic/input.c` の symbol の表に 2 つ（文字は打たない）を足した。
  zdesktop の `input.c` は元から 125・126 を Super として数えていた。
- 試験: `plan/ws035/tests/zdesktop-p014.sh`（新規）、`plan/ws035/tests/qmp-keys.py` に `super`（qcode `meta_l`）。

### 判断が要る点（戻せる既定で進めた）

- UAPI（`include/uapi/input.h`）に key code の名前 2 つを足した。値は Linux の evdev と同じ標準の code で、新しい interface ではない
  ので既定として足した。不要なら名前を消し、driver に数字で書く形に戻せる。

### 確認（2026-09-27、QEMU・Venus、lean image `plan/tools/titlebar/build-menu-image.sh`）

- `plan/ws035/tests/zdesktop-p014.sh` PASS（build/ws035-p014/）: Super+Tab で開き（`opening key`、`open windows=3`）、Tab・Shift+Tab・→ で
  current が s→b→s、Enter で s を選び（`select surface=8 via=key`、`closed`、s の色が中央に）、Super+Tab と Esc、Super+Tab 2 回で閉じ、
  閉じた後の Tab は Wiseview に取られない。moved.png（wl_shm のタイルが青い縁で現在）を見た。1 回目は Super key が届かず FAIL
  （上の driver の欠け）。
- 回帰: `plan/tools/titlebar/menu-p003.sh` PASS、`plan/tools/titlebar/menu-regress.sh` で p059・p062〜p065・p068〜p072 すべて PASS。boot test PASS
  （build/ws035-p014-boot/login.png）。
- build warning 0（kernel と zdesktop。外部 package の既存の warning は別）。style-check: shell.c 0、変えた既存の driver の file は変更前と同数
  （usb-hid.c 137、ps2-8042.c 32、input.c 163、input.h 3）。
- 未実施: PS/2 の Super key（QMP の key は USB keyboard に届く）、i915 実機。
