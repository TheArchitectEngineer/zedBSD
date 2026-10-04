<!-- awesome-plan project=zedbsd record=ws073p011 -->

# ws073-p011: BUG-070 — USB HID の keyboard が keypad・Num Lock・Print Screen などを出す

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-070](../../bugs/BUG-070.md)（ws035-p078 で見つかった。main の依頼）

## 目的と受け入れ

USB の keyboard の HID usage のうち、表に無かった key（Print Screen・Scroll Lock・Pause・Num Lock・keypad の / * - + Enter 1〜9 0 .・102nd の `<>`・
Application）が evdev の code として `/dev/input/eventN` に出る。Linux の HID の表と同じ code にする。

## 再現（修正前、QEMU。p012 の kernel）

guest の [tests/evdev-keys.c](../tests/evdev-keys.c) で全ての event device を読み、host の [tests/keys-host.py](../tests/keys-host.py) が QMP の
`input-send-event` で 31 の key を押して離す: USB の keyboard（event0）に出たのは `a`（30）と Caps Lock（58）だけ。

## 原因

`keyboard_code()`（`src/drivers/usb/usb-hid.c`）の表に usage 0x32、0x46〜0x48、0x53〜0x67、0x68〜0x73、0x85、0x87〜0x8c、0x90〜0x94 が無く KEY_RESERVED に
なっていた。`include/uapi/input.h` にもそれらの evdev の code の定義が無かった。

## 修正

- `include/uapi/input.h`: KEY_KPASTERISK・KEY_NUMLOCK・KEY_SCROLLLOCK・KEY_KP0〜9・KEY_KPMINUS・KEY_KPPLUS・KEY_KPDOT・KEY_ZENKAKUHANKAKU・KEY_102ND・
  KEY_RO・KEY_KATAKANA・KEY_HIRAGANA・KEY_HENKAN・KEY_KATAKANAHIRAGANA・KEY_MUHENKAN・KEY_KPJPCOMMA・KEY_KPENTER・KEY_KPSLASH・KEY_SYSRQ・KEY_POWER・
  KEY_KPEQUAL・KEY_PAUSE・KEY_KPCOMMA・KEY_HANGEUL・KEY_HANJA・KEY_YEN・KEY_COMPOSE・KEY_F13〜F24（Linux と同じ値）。
- `keyboard_code()`: 上の usage を Linux の `hid_keyboard` の表と同じ code に。日本語の keyboard の key（International 1〜6、LANG3〜5）も足した。
- HAL は触れていない（uapi の定義の追加だけ）。

## 検証（QEMU、KVM、qemu-xhci の usb-kbd。実機は未実施）

- build: amd64 guest の vmunix・i386 pcat の vmunix warning 0。
- `keys-host.py` と `evdev-keys.c`: 31 中 28 の key が期待の code で押下と解放の両方（Num Lock 69、keypad の全て、Print 99、Scroll Lock 70、Pause 119、
  `<>` 86、Ro 89、Yen 124、Henkan 92、Muhenkan 94、Katakana/Hiragana 93、a、Caps Lock）。残りの 3 つ（`menu`・`f13`・`f24`）は QEMU の usb-kbd が
  標準の usage を送らない（`f13` は usage 0x6c＝F17 の 187 として届き、`menu`・`f24` は何も届かない）ためで、driver の表は Linux と同じ（0x65→Compose、
  0x68〜0x73→F13〜F24）。この 3 つは実機で確かめる（未実施）。
- boot test: lean amd64（`plan/tools/gnu-utils/config-amd64-base.mk`、warning 0）PASS（`build/ws073-img/boot-test-p011/login.png`）。
- 規約: `tests/style-diff.py`（usb-hid.c・input.h）0。

## 残り

- ws035 の `plan/ws035/tests/zdesktop-p078.sh` の Num Lock の段を戻すのは WS035 の側。
- 端末（console）の keypad の文字への変換は kernel の console の keymap の範囲で、この Phase は確かめていない。
