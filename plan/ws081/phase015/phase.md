<!-- awesome-plan project=zedbsd record=ws081-p015 -->

# ws081-p015: Notes の「指で書く」の切り替え

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。Notes の toolbar の「Finger」、一本指で書く・二本指で scroll・pinch、書き始めの線の取り消し。host 試験と QEMU の guest 試験（main の pen の image）。実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29 夕「p010 を仕上げる → p015 → p006」）。Awesome Plan の Queue の item ではない
Resume point: なし
<!-- awesome-plan-current:end -->

## 目的（2026-09-29 ユーザーの決定）

既定は今（[p013](../phase013/phase.md)）のまま、指は scroll・pinch。toolbar に切り替えを足し、入れた間は一本指で線を書き、二本指で scroll・pinch する。
実機でペンが使えない場合のデモの備え。

## 受け入れ条件

1. toolbar に切り替え（「Finger」）があり、押すたびに入・切が変わり、入の間は選ばれた色（Kei の青の pill）で見える。
2. 入の間、頁の上の一本指は、選んだ道具（Pen・Marker・Eraser、色・太さ）で線を書く（消しゴムなら消す）。報告は間引かずに使い、線の形は libpdf の
   centripetal Catmull-Rom（design §3.8）で滑らかにつながる。
3. 入の間、二本指は scroll・pinch（p013 と同じ）。一本目の指が書き始めて間もなく（250 ms 未満、または 12 px 未満しか動いていない）二本目が触れたら、
   書きかけの線は取り消して（頁に残らない）、二本指の scroll・pinch になる。それより後の二本目は無視し、線はそのまま続く。
4. 入の間も toolbar の tap は button を押す。掌の規則（ペンが近い間と離れて 500 ms は新しい指を無視、ペンが近づいたら書きかけの指の線は取り消す）は保つ。
5. 切の間は p013 と同じ（指は線を書かない）。
6. host 試験・guest 試験（QEMU）で確かめる。実機は p007。

## 設計

- `touch.c`: `notes_touch_write_mode(touch, on)`。入の間、最初の指が頁の上に触れると「書く指」になり、gesture には渡さず、書く event
  （BEGIN・MOTION・END・ABORT、窓の px と ms の時刻）の queue に入れる。main は `notes_touch_take_write` で取り出す。二本目の扱いは上の 3。
  取り消した時は、書く指をその位置・時刻で gesture に渡し、二本目と一緒に scroll・pinch にする。入の間は double tap の拡大をしない（一本指の tap は点を書く）。
  glide している頁に書く指が触れたら、頁はその場で止まる。
- `main.c`: 書く event を pointer と同じ入力（`NOTES_SOURCE_POINTER`、固定の筆圧）にして `app_input` に通す（道具・色・太さ・消しゴムは pointer と同じ）。
  ABORT は書きかけの線を捨てる（`app_abort_contact`）。指の contact が pointer・ペンの contact に取って代わられたら、その後の指の event は無視する。
  ペンの近さを知らせた直後にも書く event を取り出す（ペンの down より先に指の線を取り消すため）。toolbar の action `NOTES_ACTION_FINGER`（14）。
- `ui.c`・`app.h`: toolbar の道具の群の後に「Finger」の button。`notes_ui_state.finger_write`。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/notes/touch.h`・`touch.c` | `notes_touch_write_mode`・`notes_touch_take_write`、書く event（`struct notes_touch_write`、queue 256。motion は最後の 1 つを end・取り消しのために空ける）。最初の指が頁の上なら書く指（gesture に渡さない）。二本目の扱い（`touch_write_young`・`touch_write_handover`）。compositor の cancel・ペンの接近・mode の切で書く指を終える。double tap の拡大は入の間しない。glide の停止を `touch_stop` に出し、ペンと書く指で共有。`NOTES TOUCH write …` の log |
| `main.c` | `finger_write`・`finger_contact`。`app_finger` が書く event を pointer の入力（`NOTES_SOURCE_POINTER`、`NOTES_POINTER_PRESSURE`）にして `app_input` に通す。取り消しは `app_abort_contact`（線を捨てる。`NOTES ABORT stroke`）。pointer・ペンが contact を取ったら指の後の event は無視（`app_end_contact` が `finger_contact` を 0 に）。ペンの近さを知らせた直後にも `app_finger`。action `NOTES_ACTION_FINGER` で切り替え、status に「One finger writes, two fingers scroll」／「Fingers scroll and zoom」、`NOTES FINGER write=` の log |
| `ui.c`・`app.h` | 道具の群の後に「Finger」の button（入の間は Kei の青の pill）、`NOTES_ACTION_FINGER`（14）、`notes_ui_state.finger_write` |
| `plan/ws081/design.md` §5.6 | 「指で書く」の追記 |
| `plan/ws081/tests/host-notestouch.c`（`test_write` を追加）・`p015-guest.sh`（新） | 試験（下） |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-notestouch.sh`（`-Wall -Wextra -Werror -Wconversion`） | `host-notestouch: ok (52 checks)`（p013 の 30 に 22 を足した） |
| 同上、`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1"` | ok（52 checks） |
| 足した試験の中身 | 切の間の指は何も書かない。入の間の一本指は BEGIN・17 の MOTION（報告の全部）・END を出し、始まりは触れた点、終わりは最後の報告の点、時刻は指の時刻、頁は動かない。30 ms 後の二本目は書きかけを取り消し（BEGIN・ABORT、END なし）、二本指で scroll する。60 px 動いた 100 ms 後の二本目も取り消す。同時の二本指の pinch は書かずに拡大する。400 ms・200 px 書いた後の二本目は無視され、線は続いて頁は動かない。toolbar の tap は tap のまま。double tap は点を二つ書き拡大しない。ペンの接近・compositor の cancel は取り消し。書く間に切にすると最後の報告の点で END。glide している頁は書く指の下で止まる |
| mutation（scratchpad の script、有効な 11 個: grace の時間、slop、ペンの取り消し、cancel の取り消し、切の END、glide の停止、toolbar の判定、handover の follow、MOTION の push、double tap の抑止 ほか） | 10 個が FAIL。double tap の抑止を消した mutation は残る（入の間の一本指は gesture に渡らないので double tap が起きない。同値）。compile の通らなかった 2 個は数えない |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/notes build/amd64/bin/touchinject build/amd64/dynamic/libkeiland.so`（`-Werror`） | rc=0、`warning:`・`error:` 0 |
| [p015-guest.sh](../tests/p015-guest.sh)（worktree の `build/ws081-main-pen.img` の guest に notes・libkeiland・libpdf 等・touchinject を SSH で置く。Notes fullscreen 1280x800） | **PASS**（ok 20 項目） |
| 同上の中身 | 切の一本指は書かない（0 本）。toolbar の Finger（389,34）の tap で `NOTES FINGER write=1`。一本指の drag で線 1 本（18 報告から 18 点）。同時の二本指を開くと `write abort reason=fingers`・`NOTES ABORT stroke`、1.88 倍に拡大、線は増えない。二本指の flick（994 px/s）で `drag fingers=2`、離した後に 295 px glide、線は増えない。拡大した頁に一本指で 2 本目。Finger の tap で `write=0`、その後の一本指は書かない（2 本のまま） |
| [p013-guest.sh](../tests/p013-guest.sh)（回帰、同じ guest） | **PASS**（二本指 1.912 倍、flick 1003 px/s・322 px glide、ペンの線、指は書かない、double tap、toolbar の New Page） |
| `plan/tools/style-check.py`・`plan/ws081/temp/style-extra.py`（touch.c・touch.h・main.c・ui.c・app.h・host-notestouch.c） | 指摘 0 |

画面（QEMU）は worktree の `build/ws081-shots/ws081-p015-20260929-*.png` です（元は `build/ws081-p015-guest/`）。

- `-finger-on.png`: Finger が青の pill になり、status「One finger writes, two fingers scroll」
- `-written.png`: 一本指の線
- `-zoomed.png`: 二本指の拡大の後（取り消した線は無い）
- `-flicked.png`: 二本指の flick の後
- `-written-zoomed.png`: 拡大した頁に一本指の 2 本目
- `-finger-off.png`: Finger が切に戻り、切の一本指の drag は書いていない

## 未実施・制限

- 実機（touch LCD）での手触り、250 ms・12 px の係数は未確認（p007）。
- 指の線は固定の筆圧（pointer と同じ 0.5）で、太さは変わらない。§3.8 の予測の尾（16 ms の先の仮の線）は入れていない。
- touchinject の座標は整数の px なので、guest の線は傾きによって僅かに段が見える（実機の panel の分解能では別）。
- 取り消した線の番号（stroke の id）は使い直さない（id は一意のまま飛ぶ）。
- 「Finger」は toolbar だけにあり、menu には無い。状態は保存しない（起動のたびに切）。
- boot test は main に依頼する（subagent は image を build しない）。
