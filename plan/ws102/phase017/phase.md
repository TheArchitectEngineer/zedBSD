<!-- awesome-plan project=zedbsd record=ws102-p017 -->

# ws102-p017: 編集の操作の拡張と、直前の app

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施。button は p016）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て、サブエージェント P6、worktree `ws035-keiland`（branch `wt/ws035`））
依存: p015（cleared）。keyboard の道具の面の button は p016（P3、p007 の後）

## 範囲と受け入れ

- design.md §2.10 の表の「編集の操作の拡張」と「直前の app」の、compositor と client の側の口。button は p016。
- 受け入れ: Text Editor で「select_begin → → ×5 → copy → 直前の app（2 つ目の Text Editor）→ paste」で同じ文字が入る。拡張の無い app（Terminal）への key の落とし、状態の flag。
  回帰は host の libkeiui の試験、C9、WS079-p010、boot test。
- keyboard.c は変えない（変えていない）。IME の file（ime.h・text-input.c・input-method.c）と seat.c の IME の hook は触らない（触っていない）。

## protocol（`keiland_edit_v1`、version 1）

| object | request / event | 引数 |
| --- | --- | --- |
| `keiland_edit_manager_v1`（global、zdesktop の 23 番） | 0 `destroy` | — |
| | 1 `get_edit` | `new_id keiland_edit_v1`、`object xdg_toplevel` |
| `keiland_edit_v1` | 0 `destroy`、1 `set_state`（request） | `uint actions`（bit 1 << 操作）、`uint state` |
| | 0 `action`（event） | `uint action` |

- 操作: 0 copy・1 cut・2 paste・3 undo・4 redo・5 select_all・6 select_begin・7 select_end。
- state の bit: 1 選択がある・2 貼り付けられる・4 取り消せる・8 やり直せる・16 選択の途中（select_begin から select_end・copy・cut まで）。

## compositor の口（`userland/desktop/wayland/edit.c`、宣言は `zwl.h`）

| 関数 | 動き |
| --- | --- |
| `int zwl_edit_action(server, action)` | focus の窓へ送る。拡張があってその操作を出来る窓には action の event。無い窓には key（`zwl_seat_key` を通るので窓の menu の shortcut も効く）: Ctrl+C・X・V・Z・Y・A。app の id が `terminal`・`zterm`・`xterm` の窓は copy を Ctrl+Shift+C、paste を Ctrl+Shift+V にし、他の操作は送らない。select_begin・select_end は拡張の無い窓には送らない（p016 の keyboard の Shift の toggle が受け持つ）。戻り値 0・ENOENT（focus 無し）・ENOTSUP（送れない） |
| `int zwl_edit_state(server, &enabled)` | focus の窓が今出来る操作（bit 1 << action）。拡張のある窓は、出来る操作を状態で絞った物（copy・cut は選択がある時、paste は貼れる時、undo・redo は出来る時、select_begin は選択の途中でない時、select_end は途中の時）で 1 を返す。拡張の無い窓は key のある操作で 0 を返す。focus 無しは -1 |
| `int zwl_focus_previous(server)` | 今の desktop の窓で、重なりの順（raise と focus が一緒に動く）の 2 番目を前に出し focus を移す。もう一度呼ぶと戻る。keyboard は閉じない |

- log: `ZWL EDIT create`・`ZWL EDIT state client= edit= actions= flags=`・`ZWL EDIT action=… via=protocol|keys|none …`・`ZWL FOCUS previous surface= app= from=`。
- 試験の口（p016 の button の代わり）: Super+Alt と C・X・V・Z・Y・A（操作）・S（select_begin）・E（select_end）・P（直前の app）・Q（`zwl_edit_state` を log に出す）。`shell.c` の `zwl_glass_key` の Super+Tab の後に 3 行。
- 他: `protocol.c`（global と dispatch）、`objects.c`（toplevel が消える時に edit から外す）、`edit.h`、`Makefile`。

## client の側

- libwayland: `edit-protocol.c`・`zed-edit-v1-client-protocol.h`（新規、手書き）、`Makefile`、`exports.map`。
- libkeiland（**KEILAND_VERSION 19**）: `edit.c`（新規）の `keiland_edit_create(display, toplevel, callback, data)`・`keiland_edit_set_state(edit, operations, state)`（変わった時だけ送る）・
  `keiland_edit_destroy`。`include/libc/keiland.h`、`Makefile`、`exports.map`。
- libkeiui（**KUI_VERSION 8**）: `edit.c`（新規）と `window.c`・`window.h` の数行。`kui_window` が窓を作る時に edit を作り、全ての操作を出来ると言い、状態を待つ前（`kui_window_dispatch` の頭）に送る。
  - 既定の動き: 操作を、それが表す key として窓自身の key の入力に積む（copy Ctrl+C・cut Ctrl+X・paste Ctrl+V・undo Ctrl+Z・redo Ctrl+Shift+Z・select_all Ctrl+A）。
    select_begin・select_end は library の mode: 選択の途中は caret を動かす key（矢印・Home・End・PgUp・PgDn）に Shift を足す。copy・cut で終わる。
  - 状態: app が `kui_window_edit_state(window, flags)` で言えばそれ。言わなければ、選択がある・取り消せる・やり直せるを「ある」とし、貼れるかは clipboard（`kui_window_can_paste`）から。
  - app の callback `kui_window_on_edit(window, fn, data)`（1 を返すと既定の動きをしない）、`kui_window_selecting(window)`。
  - **Text Editor は変更なしで対応した**（key を受ける app なので）。
- 注: `kui_ui` の text の view の Ctrl の key を持たない app（key で copy をしない app）には既定の動きは効かない。その app は callback か `kui_window_edit_state` で対応する。

## 試験（`plan/ws102/tests/`）

- `edit-guest.sh IMAGE OUTDIR`（QEMU の Venus、p015 の `build-inset-image.sh` の image）:
  1. Text Editor A（"HELLOWORLD"）と B（空）、B が上。QWERTY を開き、直前の app で A が前に（keyboard は閉じない）。
  2. A で select_begin（protocol）、Right ×5、Super+Alt+Q で `enabled=0xbb`（paste・select_begin が灰色）、copy（protocol）、`enabled=0x7f`（select_end が灰色）。
  3. 直前の app で B、paste（protocol）、Ctrl+S で `b.txt` が "HELLO"。
  4. Terminal: copy が `via=keys key=46 modifiers=0x5 terminal=1`、cut は `via=none reason=no-keys`、`enabled=0x5`。wlshm: copy が `modifiers=0x4 terminal=0`、`enabled=0x3f`。どちらも動き続ける。error 0。

## 結果

| 確認 | 結果 |
| --- | --- |
| build: 試験の image `build/ws102-p015`（`build/ws099/p017.img`）、criteria の image（`build/ws099/p017-criteria.img`） | どちらも exit 0、`userland/desktop` の warning 0（main から来た openssl の warning は範囲外）、`git diff --check` 0 |
| `edit-guest.sh` 1 回目（Super+Alt+Q の前の版） | **PASS**: select_begin・copy・paste が protocol で、`b.txt` = "HELLO"、keyboard は開いたまま、Terminal・wlshm は key |
| `edit-guest.sh` 2 回目（`build/ws102-p017-shots/run2/`、Q を足した版） | **PASS**: 上に加えて `enabled=0xbb`（選択の途中）→ `0x7f`（copy の後）、Terminal `0x5`、wlshm `0x3f`。state の log は A の flags `0xd` → `0x1d`（選択の途中）→ `0xf`（copy の後、貼れる）。error 0 |
| host の libkeiui の回帰: `host-input`・`host-widgets`・`host-draw`・`host-chooser`・`host-inset` | 全て **PASS**。libkeiui の `edit.c` の host の試験は無い（window と libkeiland に依る。guest の試験が受け持つ） |
| C9 | 9 本 PASS、p076 が 1 回 FAIL（手順 7 の title の帯の drag の move が起きず、後の resize が続けて失敗）。同じ image で p076 だけを 2 回流して 2 回とも **PASS**（`build/ws099/p017-p076-1`・`-2`）。p076 は ws099-p009 でも 1 回の不安定さが記録されている。この Phase の変更（Super+Alt の key、edit の object）は p076 の経路に無い |
| WS079 `zdesktop-p010.sh` | **PASS** |
| boot test | **PASS**（`build/ws099/p017-regress/boot/login.png`） |

画面: `build/ws102-p017-shots/run2/previous.png`（直前の app で A が前、QWERTY は開いたまま）、`selected.png`（5 文字の選択）、`pasted.png`（B に "HELLO"）、`keys.png`（Terminal と wlshm）。Q1 に SendMessage で送った。

## 残り

- keyboard の button（p016、P3）: `zwl_edit_action`・`zwl_edit_state`・`zwl_focus_previous` を呼ぶ。Super+Alt の試験の口は残す（試験が使う）。
- 状態の既定（選択・取り消し・やり直しを「ある」とする）は、Text Editor が `kui_window_edit_state` で本当の状態を言えば灰色が正しくなる（Text Editor の変更、この Phase の外）。
- 直前の app の長押しの一覧（design の「後」）は作っていない。
