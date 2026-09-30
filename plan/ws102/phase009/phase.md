<!-- awesome-plan project=zedbsd record=ws102-p009 -->

# ws102-p009: touch の ROUTE_OSK（L2 の (b)、多指の連打）

Status: cleared（2026-09-30、QEMU の Venus（pen の image の注入の touch）と host。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main（Q1）の依頼（2026-09-30、P4、worktree `.claude/worktrees/ws090-widgets`、branch `wt/ws090`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §2.6・§3 の L2 の (b)。`touch.c` に `ROUTE_OSK` を足す。
- panel の上に落ちた指は、shell が他の指を持っていても、指ごとに独立した key の press として keyboard に渡す（`zwl_keyboard_touch_down/motion/up`）。
- keyboard.c の変更は、指ごとの press を受ける入口の関数とその状態だけにする。key の表・描画・送出には触らない（P3 が作業中）。
- IME の file（ime.h・text-input.c・input-method.c）と seat.c の IME の hook には触らない。

受け入れ: 2 本の指を 50 ms ずつ重ねた 100 打鍵で、取りこぼし 0。回帰は L1・L2 の全ての手順、C9、WS079-p010、boot test。

## 変えたもの

**`touch.c`**
- `ROUTE_OSK` を足した。
- 指が触れたとき（`contact_begin`）、他の判断より先に確かめる:
  - 開いた panel の上（`zwl_keyboard_at`）で、下の角の 28 px の swipe の場所でない指は、`zwl_keyboard_touch_down` に渡す。
  - keyboard が取れば、その指は離れるまで `ROUTE_OSK` になる。
  - 下の角は、panel が角まで届いていても、今までどおり角の swipe（shell）に行く。そうしないと、開いた flick の panel を角からの swipe で閉じられない（初めの試しで手順 touch が失敗した）。
- 動きは `zwl_keyboard_touch_motion`、離しは `zwl_keyboard_touch_up` に渡す。screen が消えたときは `zwl_keyboard_touch_cancel` に渡す。
- log: `ZWL TOUCH osk contact=`。
- `ROUTE_OSK` は shell の指ではないので、他の指の扱い（`shell_busy`）は変えていない。

**`keyboard.c`（入口の関数と状態だけ）**
- 状態: `touch_owner`（panel の press を持つ指の id + 1、0 は無し）と `touch_x`・`touch_y`（その指の最後の位置）。
- 入口の関数:

  | 関数 | 働き |
  | --- | --- |
  | `zwl_keyboard_touch_down` | 別の指が press を持っていれば、先にその指の最後の位置で離す（その key が働く。rollover）。それから新しい指の press を始める |
  | `zwl_keyboard_touch_motion` | press を持つ指だけが、press を動かす（flick の花びら・手書きの線） |
  | `zwl_keyboard_touch_up` | press を持つ指なら、その位置で離す（key が働く）。rollover で済んだ指は何もしない |
  | `zwl_keyboard_touch_cancel` | 働かせずに press を終える |

- press と離しは、今の `keyboard_panel_button` に、指の位置を pointer の位置として一時的に渡す（`keyboard_touch_button`）。pointer は動かさない。
- `zwl_keyboard_tick` の「ボタンが離されていれば press を消す」は、指の press には当てない。
- `zwl_keyboard_close` は `touch_owner` も消す。
- mouse の press が panel を持っている間は、指は取らない。
- log: `ZWL OSK touch down id= x= y= taken= rollover=`、`ZWL OSK touch up id= … acted=`、`ZWL OSK touch cancel id=`。
- key の表・描画・送出の code には触れていない。

**`zwl.h`**: 上の 4 つの関数の宣言。

**`plan/ws102/tests/osk-guest.sh`**: 手順 `roll` を足した。
- Text Editor（`/root/roll.txt`）に向けて、QWERTY の panel の f と j を注入の 2 本の指で交互に 100 回打つ。
- 各指は、前の指が離れる 50 ms 前に触れ、さらに 50 ms で次の指が触れる。
- 判定:
  - file が「fj」× 50 になる。
  - `ZWL OSK qkey` が 100 行ある。
  - rollover が 99 回起きる。
  - 画面 `roll.png` を撮る。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/wayland …` | rc 0、warning 0 |
| style | `plan/tools/style-check.py touch.c keyboard.c zwl.h` | 0 件 |
| host | `plan/ws102/tests/host-keyboard.sh` | PASS |
| 変更の前（受け入れの試験が意味を持つこと） | main の wayland（この変更なし）で `osk-guest.sh … start roll` | FAIL: 「f」が 50 文字だけ、key 50 行、rollover 0（2 本目の指は shell が 1 本目を持っている間、どこにも行かなかった） |
| **受け入れ** | この変更で `osk-guest.sh … install start roll` | **PASS: fj × 50 の 100 文字、key 100 行、rollover 99、取りこぼし 0** |
| 回帰: L1・L2 の全ての手順 | `osk-guest.sh build/ws102-shots/p009-all2 install start pointer flick edges touch send close qwerty hand roll`（pen の image `build/main-pen/hdd-image.img` の複写） | PASS |
| 回帰: 1920x1080 | `VENUS_SIZE=1920x1080` の guest で `… install large` | PASS |
| 回帰: C9 | `plan/ws099/tests/criteria.sh build/ws102-p009-criteria.img build/ws102-p009/criteria C9`（`build-criteria-image.sh` で作った image） | 10 本すべて PASS |
| 回帰: WS079-p010 | `plan/ws079/tests/zdesktop-p010.sh build/amd64 …`（criteria の image の guest） | PASS |
| boot test | `OUTPUT=build/ws102-p009-boot plan/tools/boot-test.sh build/ws102-p009-criteria.img` | PASS |

回帰の途中の失敗（どちらも直さずに解けた）:
- 全ての手順の 1 回目で、手順 qwerty が失敗した。記号の面の key の位置の log（`qrect face=symbols`）が 1 行も出ず、qwerty-plan.py が「+」を見つけられなかった。この手順は pointer を使うので、この Phase の変更は通らない。qwerty だけの run 2 回と、全ての手順の 2 回目は PASS。1 回だけの不安定さとして記録する。
- WS079-p010 の 1 回目は、guest の起動の直後に走らせて失敗した（network の icon の位置が未確定、`x=0..-1`）。起動を 30 秒待つと、変更の前の wayland でも後の wayland でも PASS。

## 残り

- 実機（5330 の touch の panel）での 2 本の親指の連打は未実施（L4 の p013）。
