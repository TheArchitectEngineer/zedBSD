<!-- awesome-plan project=zedbsd record=ws102-p004 -->

# ws102-p004: 文字の送出

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`）

## 範囲と受け入れ

design §2.5 の送出を扱う。次の 4 つを送る。

- US の key で表せる文字は evdev の key として送り、Shift も付ける。
- それ以外の文字は、text-input の commit として送る。
- IME が組み立て中のときと、text-input の無い app へは送らない。
- 濁点の key と大小の key で、直前の 1 文字を置き換える。

受け入れ（L1 の (c)）:

- 注入の操作で打った「aiueo123」が、Text Editor の file に誤り 0 で届く。今回は大小の key を含めて「aiueO123」を打った。
- 「あいうえお」が、ime-probe に誤り 0 で届く。

かなは ime-probe で確かめ、Text Editor では英数だけを確かめる（main の指示）。IME の file と seat.c は変えない。

## 実装

| file | 内容 |
| --- | --- |
| `keyboard-layout.c`・`keyboard.h` | `zwl_flick_us_key`: 1 文字の ASCII を、US の key の evdev の code と Shift にする。改行は Enter（28）。表は zdesktop の keymap と同じ並び。<br>`ZWL_FLICK_KEY_BACKSPACE`・`_ENTER`・`_SPACE` |
| `keyboard.c` | `keyboard_send`: US の key があれば `keyboard_send_key` で送る。Shift が要るときは、その間だけ `server->modifiers` に Shift を立てて `zwl_seat_modifiers`、key の press と release を `zwl_seat_key_deliver`、終わったら modifiers を戻す。<br>US の key が無ければ `keyboard_send_commit` で送る（`zwl_text_input_current` と `zwl_text_input_deliver`、ime.h の公開の API）。<br>送らないとき: focus が無い（`refused reason=no-focus`）、text-input が無い（`no-text-input`）、IME が組み立て中（`composing`）。<br>濁点: 直前に commit したかなを、`delete_surrounding_text`（before=前のかなの byte 数）と次の形の commit で置き換える。<br>大小: 直前に key で送った英字を、Del と、もう一方の大小の key で置き換える。<br>Del の key。log `ZWL OSK send via=key\|commit …` |

変えていないもの: `ime.h`・`text-input.c`・`input-method.c`・`seat.c`。ime.h は include して、公開の関数と `struct zwl_ime` の `composing` を読むだけ。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland bin/textedit bin/ime-probe bin/wltest dynamic/libkeiui.so` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c keyboard-layout.c keyboard.h` | 0 件 |
| host | `sh plan/ws102/tests/host-keyboard.sh`（p004 の分を足した: a・A・1・!・空白・改行・\\・\| の key と Shift、あ は key 無し、英字と数字の face の ASCII の全ての文字に key がある） | PASS |
| guest（QEMU の Venus、pen image の複写。この worktree の compositor・Text Editor・ime-probe・wltest・library を入れる） | `plan/ws102/tests/osk-guest.sh build/ws102-shots/p004 install start pointer flick edges touch send` | PASS（p002・p003 の手順を含む） |

手順 send で確かめたこと:

- Text Editor（`/root/osk.txt`）で、英字の face の a・i・u・e・o、大小の key（o → O）、数字の face の 1・2・3 を打った。物理の Ctrl+S で保存し、SSH で読むと file は `aiueO123` だった（`send-textedit.png`）。key は `code=30 shift=0`（a）、`code=24 shift=1`（O）、`code=4`（3）で送られた。
- ime-probe（text-input の app）で、かなの face の あ・い・う・え・お、か、濁点の key を打った。probe の text は `あいうえおかが` で、が の前に `PROBE DELETE before=3 after=0` があった（か を消して が）。compositor の log は `send via=commit text=が before=3`。
- wltest（text-input 無し）に かな を打つと、`refused reason=no-text-input` で送らなかった。
- （参考。L1 の条件ではない）Text Editor に かな（あ・い）を打つと、WS090 の text-input で受け取り、保存した file に `あい` があった。

## 見つけたこと・制限

- D1（Text Editor へのかな）は、WS090 の text-input（main に取り込み済み）で、今の Text Editor が既に受け取れた（上の参考）。L1 の受け入れは、main の指示どおり ime-probe で判定した。
- Shift を付けて送る間、`zwl_seat_modifiers` は IME の grab にも modifiers を知らせる（seat.c の既存の動き）。keyboard から Shift を送っても IME の状態は変わらないが、IME の作業の後でもう一度確かめる。
- IME が組み立て中のときは送らない（L1）。IME と組んだ変換は D2（L3 の p012）で扱う。
- WS099 の C9、WS079-p010、boot test は p005（L1 の仕上げ）でまとめて流す。p004 は `keyboard.c` の中だけを変えていて、hook は p002 から変わっていない。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は p005（L1 の仕上げ）。
