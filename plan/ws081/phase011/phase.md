<!-- awesome-plan project=zedbsd record=ws081-p011 -->

# ws081-p011: Terminal への適用（scroll と慣性、長押しからの選択）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。Terminal の wl_touch、scrollback の px 単位の慣性の scroll・rubber band・catch、tap の click、長押しからの単語の選択。host 試験と QEMU の guest 試験（main の pen の image）。実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p010（Files）→ p011（Terminal）の慣性の scroll」）。p010 は main の判断待ちで止めたので、独立した p011 を先に行った。Awesome Plan の Queue の item ではない
Resume point: なし
<!-- awesome-plan-current:end -->

## 決定

- Terminal は wl_touch を bind します。compositor は指を pointer の代わりとして渡さなくなるので、指の操作は Terminal の側で作ります。
- 一本指（二本指でも重心で同じ）の drag: scrollback の scroll。
  - libkeiland の scroller を使います。慣性・rubber band・catch があります。
  - 位置は live の画面からの px で、画面には行（`view`）と行の中の px の offset（`view_offset`）で見せるので、行の単位ではなく 1 px ずつ動きます。
  - live の画面より先では offset が負（文字が上に動く）、最も古い行より先では offset が 1 行以上になり、どちらも離せばばねで戻ります。
- tap: 左の button の click（押す・離す）です。二回の tap は Terminal の double click（単語の選択）になります。glide を止めた tap は click しません。
- 長押し（500 ms）: 押す・離す・押すを同じ時刻で作ります。つまり押したままの double click で、指の下の単語を選びます。その後の drag は、pointer の選択と同じく単語の単位で選択を伸ばし、指を離すと button を離します。長押しの後の drag は scroll しません。
- key・wheel（行の単位、offset を 0 に）・新しい出力（scrollback の view を文字の上に保つ）・別の tab が view を変えたら、指はそれを引き取ります。glide はそこで止まり、指があればそこから drag を続けます。
  - 新しい出力が続く間に scrollback を glide すると、出力のたびに止まります（下の制限）。
- 選択した文字の drag（`clipboard.c` の `start_drag`）は、指からは compositor が受けません（phase010 の依存と同じ）。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/terminal/touch.h`・`touch.c`（新） | 上の決定。Wayland と Vulkan に依存しない（host で試験できる）。`terminal_touch_layout`（画面・行の高さ・scrollback・grid の高さ・view と offset）、`_event`、`_tick`、`_view`（新しい view）、`_take_pointer`（作った pointer の event）、`_clock`。`ZTERM TOUCH …` の log |
| `window.c`・`terminal.h` | seat の TOUCH の capability で `wl_touch` を bind（version 5 まで）。down（serial つき）・up・motion・cancel を touch の queue（256）に入れる。画面に `view_offset`。閉じる時に `wl_touch_destroy` |
| `main.c` | 毎回の round で、画面の view を指に渡し、指の event を渡し、tick し、指が動かした view を画面に置き、指が作った pointer の event を pointer の queue に足す（`main_pointer` がそれを今までの選択の処理で扱う）。tick の待ちを loop の timeout に入れる。`main_cell` は offset の分だけ上下をずらして cell を求める。key を打った時の live への戻りは offset も 0 に |
| `screen.c` | `terminal_screen_scroll_view`（wheel・Shift+Page）は offset を 0 にする |
| `render.c` | 行を offset の分ずらして描き、上下に途中の行を描く（scrollback と live の画面の外は描かない）。vertex の容量を 4 行増やし、容量を越えそうな行は描かない |
| `Makefile` | `touch.c` |
| `plan/ws081/tests/host-termtouch.c`・`run-termtouch.sh`・`p011-guest.sh`（新） | 試験（下） |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-termtouch.sh`（touch.c と libkeiland、`-Wall -Wextra -Werror -Wconversion`） | `host-termtouch: ok (20 checks)` |
| 同上、`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1"` | ok（20 checks） |
| 同上の中身 | flick で古い行へ進み、離した後も 1 px ずつ（行の単位ではなく）glide し、3000 px/s の fling の距離（±15%）で止まる。offset は 1 行の内にある。scroll は pointer の event を作らない。最も古い行の先へ flick すると越えて、最も古い行にちょうど止まる。live の画面の先へ引くと指より少なく伸び、ばねで戻る。tap は同じ点で押す・離す（down の serial つき）。glide を止めた tap は何も押さない。長押しは同じ時刻の押す・離す・押す。その後の drag は pointer の motion を作り、離すと release し、view は動かない。glide 中の出力は glide を止める。key の live の画面は保たれる。同じ view の別の tab に替わると glide が止まる。別の tab の view を引き取ってそこから drag する |
| mutation（scratchpad の script、8 個: catch の tap、長押し、長押しの後の drag、外の view の検出、scrollback の範囲、行の分け方、fling の速度、画面の token） | 8 個すべてが FAIL |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/terminal`（`-Werror`） | rc=0、`warning:`・`error:` 0 |
| [p011-guest.sh](../tests/p011-guest.sh)（main の pen の image を worktree に複写して起こし、terminal・touchinject・libkeiland を SSH で置く。image の compositor。terminal は 400 行を出して待つ command） | **PASS**。一本指の下への flick（2003 px/s）で scrollback に入り、離した後に 34 行先まで glide した（`view=35`）。tap は click（`ZTERM TOUCH tap`）。長押しで `ZTERM TOUCH select` と `ZTERM SELECT how=word`（画面で「344」が選ばれている）。上への flick で live の画面の近くへ戻る（35 → 3 行）。compositor の ERROR/FAILED 0 |
| `plan/tools/style-check.py`・`style-extra.py`（touch.c・touch.h・render.c・screen.c・window.c・terminal.h・host-termtouch.c、main.c の足した部分） | 指摘 0。main.c の `variable-comment`（一つの comment でまとめた既存の変数の群）と window.c の `joined-check` は既存の行で、この Phase では変えていない |

画面（QEMU）は `/home/awe/zedBSD-rpi4/build/ws081-shots/` の `ws081-p011-20260929-*.png` です。

- `-live.png`: live の画面
- `-scrolled.png`: glide の後。行の途中で止まり、上端の行が半分見える
- `-selected.png`: 長押しで単語「344」が選ばれている
- `-back.png`: 上への flick の後

## 未実施・制限

- 実機は未確認（p007）。
- 新しい出力が続く間の glide は、出力のたびに止まる（scroller の位置を文字に合わせて動かす API が keiland に無い。要るなら scroller に「位置と範囲をずらす」API を足す）。
- 選択した文字の指の drag は compositor が受けない（phase010 の依存と同じ）。
- 二本指の拡大（文字の大きさ）は入れていない。
- boot test は main に依頼する（subagent は image を build しない）。
