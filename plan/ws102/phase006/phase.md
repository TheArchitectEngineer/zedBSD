<!-- awesome-plan project=zedbsd record=ws102-p006 -->

# ws102-p006: QWERTY の面と左下の gesture（L2 の (a)）

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`、`git merge main -m WIP` の後）

## 範囲と受け入れ

QWERTY の panel の本体を作る。左下の gesture は p002 で既にある。

- shift（1 回の shift と latch）・記号・数字・⌫・⏎・矢印。
- p020 の補助の key の列（Esc・Tab・Ctrl・`|`・`~`）を、後から panel の上に載せられる場所を残す。

受け入れ（design §3 の L2 の (a)）: 注入の 30 文字（英大小・記号を含む）を 5 文字/秒で打って、誤り 0。

回帰: C9、WS079-p010、boot test。

## 実装

| file | 内容 |
| --- | --- |
| `keyboard.h`・`keyboard-layout.c` | `struct zwl_qwerty_key`（label・Shift の label・文字・Shift の文字・操作・矢印の evdev の code・4 分の 1 key の単位の幅）。<br>2 つの面:<br>・英字の面: 数字の行（Shift で `!@#$%^&*()`）、3 行の英字（Shift で大文字）、Shift・Del、space の行（?123・`,`・space・`.`・Enter・← ↑ ↓ →）。<br>・記号の面: 数字の行、`- / : ; < > [ ] { }`、`. , ? ! ' " ` _ \ \|`、`~ + = * # % ^ &`・Del、space の行（ABC ほか）。<br>1 行は 40 単位で、狭い行は中央に寄せる。`zwl_qwerty_row`・`zwl_qwerty_face_name`。<br>`ZWL_FLICK_SHIFT`・`ZWL_FLICK_ARROW`、`ZWL_KEY_UP/LEFT/RIGHT/DOWN`（evdev 103・105・106・108） |
| `keyboard.c` | QWERTY の key の矩形（帯の下を 5 行で分ける。上に `KEYBOARD_QWERTY_EXTRA_ROWS`（今は 0）の行を空ける、p020 の場所）、key の特定、押した key の青と吹き出し（文字の key）。<br>離したときの動き:<br>・文字: Shift のときは Shift の文字。1 回の Shift は 1 文字で消える。<br>・Shift: 1 回で次の 1 文字、400 ms 以内の 2 回で lock、もう一度で off。<br>・面の切り替え: Shift も消える。<br>・Del、矢印（evdev の key）。<br>送出は p004 の `keyboard_send`（US の key と Shift）。<br>log: 開いた時と面を替えた時に `ZWL OSK qrect …`（試験が key を探す）。離した key は `ZWL OSK qkey … ms=`（打つ速さを測る）。Shift は `ZWL OSK shift state=` |

IME の file と seat.c は変えていない。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland bin/textedit bin/ime-probe bin/wltest dynamic/libkeiui.so` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c keyboard-layout.c keyboard.h`、`git diff --check` | 0 件 |
| host | `sh plan/ws102/tests/host-keyboard.sh`（p006 の分を足した） | PASS。英字の両方の大小 52・数字 10・記号 32・空白が QWERTY で打て、どれも US の key がある。行は 40 単位・12 key 以内 |
| guest: QWERTY（新しい手順、pen image の複写） | `osk-guest.sh build/ws102-shots/p006-final install start pointer flick edges touch send close qwerty` | PASS（下。L1 の全ての手順も PASS） |
| guest: 1920x1080 | `VENUS_SIZE=1920x1080 … OSK_WIDTH=1920 OSK_HEIGHT=1080 osk-guest.sh … install large` | PASS |
| 回帰: WS079-p010 | `zdesktop-p010.sh build/ws102-amd64 …` | PASS |
| 回帰: WS099 の C9 | criteria image の複写で `criteria.sh … C9` | 10 本すべて PASS |
| boot test | `plan/tools/boot-test.sh build/ws102-c9.img` | PASS |

手順 qwerty で確かめたこと:

- Text Editor に "Hello, World! Kei 2026 (a+b)=c"（30 文字。大文字は Shift、`!` と `(` `)` は数字の行の Shift、`+` と `=` は記号の面）を打った。
  - 打つ操作は `qwerty-plan.py` が log の key の場所から組み立て、4.8 秒に並べた。
  - compositor の log では、最初の key から最後の key まで 4890 ms で、5 文字/秒より速い。
  - 物理の Ctrl+S で保存した file は、打った文字列と同じだった（誤り 0）。
- Shift の 2 回で lock（`shift state=2`）になり、a b c が ABC になった。もう一度で off（`state=0`）になった。
- ← が evdev の 105 で送られた。

画面: `build/ws102-shots/p006/qwerty.png`（英字の面と Text Editor）、`qwerty-symbols.png`（記号の面）。

## 見つけたこと・制限

- 試験の誤りで 2 回直した。
  - 30 文字を 6 秒に並べると、QMP の手間で 6.1 秒になった。4.8 秒に並べて、測った値は 4.9 秒。
  - key ごとに guest から log を読み直すと、SSH の読み取りが空のときに (0,0) を叩き、App Home を開いた。場所は 1 回だけ読み、見つからなければ叩かずに FAIL にした。
- 白い glass の上の数字の行の key の地が淡い（`qwerty.png`）。L3 の見た目で扱う（p003 からの注）。
- 補助の key の列（p020）は場所だけ空けた（`KEYBOARD_QWERTY_EXTRA_ROWS`）。行の高さの配分は p020 で決める。
- 2 本指の連打（L2 の (b)）は p009、作業の領域（(d)）は p007。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は L2 の他の Phase（main の判断）。
