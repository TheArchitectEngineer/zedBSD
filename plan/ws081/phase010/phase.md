<!-- awesome-plan project=zedbsd record=ws081-p010 -->

# ws081-p010: Files への適用（tap・長押し・scroll と慣性）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。Files の wl_touch、items と sidebar の慣性の scroll・rubber band・catch、tap の click・二回の tap の open、長押しの context menu、長押しからの指の drag and drop（p014 の compositor）。host 試験と QEMU の guest 試験（main の pen の image）。実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p010（Files）→ p011（Terminal）の慣性の scroll」、同日夕「p010 を仕上げる → p015 → p006」）。Awesome Plan の Queue の item ではない
Resume point: なし
<!-- awesome-plan-current:end -->

## 見つけた依存（実装の前）

design §4.1 の (a) では、wl_touch を bind した client は、指を pointer の代わりとして受けなくなります（compositor の `touch.c`）。Files で慣性の scroll を作るには
wl_touch が要り、tap（click）・長押し（context menu）・drag（scroll か pointer の drag）を Files の側で作り直すことになります。そのうち次のことを確かめました。

- **context menu**: compositor（`menu.c`）は最新の press の serial で開きます。wl_touch の down も `press_serial` を更新するので（`touch.c`）、指の down の serial で開けます。
- **drag and drop**: compositor の `start_drag`（`data.c`）は、pointer の button が押されている（`server->buttons_down`）時だけ drag を受けます。drag はその後
  pointer に付いて動きます。wl_touch の client の指は pointer の button を押さないので、Files が指で drag and drop（file を folder・sidebar・他の窓へ運ぶ）を
  始めると、compositor が拒みます（`ZWL DATA drag refused`）。今は Files が wl_touch を持たず、指は compositor の pointer の代わりとして button を押すので、
  指の drag and drop ができています。Files に wl_touch を入れると、これが後退します。
- 直すには compositor の `data.c` で、wl_touch の client の指の暗黙の grab（down の serial）から drag を始め、drag をその指に付けて動かし、指の up で drop する
  必要があります。この subagent に許された compositor の範囲（`touch.c` と入力の処理）の外です。

## 要る判断（main へ）

1. compositor の `data.c` に touch の drag and drop を入れる（Phase を足す）。Files の p010 はその後。
2. Files の指の drag and drop を諦めて p010 を進める（長押しの後の drag は範囲選択だけ、など）。
3. Files は今の pointer の代わりのまま（慣性なし）にし、p010 を取りやめる。

既定の案（意見）: 1。file manager の指の drag and drop は中心の操作で、Terminal・Notes 等の他の app の指の drag（選択の文字の drag）にも同じ仕組みが効きます。

## 確認

実装に入っていないので、試験はありません。調べたのは compositor の `data.c`（`start_drag`）、`menu.c`（context menu の serial）、`touch.c`（`press_serial`）と、
Files の scroll の持ち方（`tab->scroll`・`sidebar_scroll` の整数の px。範囲の外の値も描画の計算は壊さない）です。

## 経過（2026-09-29）

- main の判断は (1)。compositor の指の drag and drop は [p014](../phase014/phase.md) で入れた（cleared）。その後に p010 を実装した。
- 最初の枠は実装の途中で上限に達し、変更を WIP の commit（0cf21f2f）で残した。次の枠で規約の指摘を直し、試験を流して仕上げた。

## 決定

- Files は wl_touch を bind します。compositor は指を pointer の代わりとして渡さなくなるので、指の操作は Files の側（`touch.c`）で作ります。
- 最初の指が触れた所で、その touch の意味を決めます（`main_touch_area`、最後の frame の hit の判定）。
  - items（content）か sidebar の上: gesture。一本指（二本指でも重心で同じ）の drag はその area の scroll です。libkeiland の scroller で慣性・rubber band・catch があります。
  - それ以外（toolbar・tab・header・field・dialog・Quick Look・情報の card）: 最初の指が左の button です。触れた所で押し、動けば pointer が動き、離した所で離します。
- tap: 左の button の click。二回の tap は Files の double click（open）です。glide を止めた tap は click しません。
- 長押し（500 ms）:
  - 動かずに離すと右の button の click（context menu）です。menu の serial は指の down の serial です（compositor の `press_serial`）。
  - 長押しの後に動くと、押した所で左の button を押したままにし、指に付けて動かします。Files の今までの pointer の drag（item の drag、rubber band）になり、窓の外へ出れば zdesktop の drag and drop（p014）です。
  - 長押しの後の drag は scroll しません。
- wheel・key・別の tab や folder が scroll を変えたら、指はそれを引き取ります。glide はそこで止まり、指があればそこから drag を続けます。
- libkeiland の `motion.c`: 時刻を遅れて読む program（描画で忙しい Files）が学んだ長い delay を、resampling の「frame の後ろの時刻」に使わないようにしました（`MOTION_DELAY_LONGEST` 25 ms か 1 周期の長い方で打ち切る）。
  打ち切らないと、止まっていた指が上へ動き出した時に、fit した曲線の曲がり目を描いて一瞬下へ動いて見えました（host-scroll の `test_late_reader`）。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/files/touch.h`・`touch.c`（新） | 上の決定。Wayland と Vulkan に依存しない（host で試験できる）。`fm_touch_layout`（area の scroll・範囲・高さ・token）、`_event`、`_tick`、`_scroll`（新しい scroll）、`_take_pointer`（作った pointer の event）、`_clock`。`ZFILES TOUCH …` の log |
| `window.c`・`window.h` | seat の TOUCH の capability で `wl_touch` を bind。down（serial つき）・up・motion・cancel を touch の queue（256）に入れる。閉じる時に `wl_touch_destroy` |
| `main.c` | 毎回の round（`main_touch_round`）で、items と sidebar の scroll を指に渡し、新しい指の下を調べて event を渡し、tick し、指が動かした scroll を置き、指が作った pointer の event を Files の event（`fm_ui_event`）として渡す。press は window の `button_serial` を指の serial にする。zdesktop の drag and drop の間の release は捨てる（drag の終わりは `FM_EVENT_DRAG_DONE`）。tick の待ちを loop の timeout に入れる |
| `Makefile` | `touch.c` |
| `userland/desktop/libkeiland/motion.c` | `MOTION_DELAY_LONGEST`（上） |
| `plan/ws081/tests/host-filestouch.c`・`run-filestouch.sh`・`p010-guest.sh`（新）、`host-scroll.c`（`test_late_reader` を追加） | 試験（下） |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-filestouch.sh`（touch.c と libkeiland、`-Wall -Wextra -Werror -Wconversion`） | `host-filestouch: ok (20 checks)` |
| 同上、`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1"` | ok（20 checks） |
| 同上の中身 | toolbar 等の指は pointer の motion・press・release を作り、scroll しない。flick で items が下へ進み、離した後も glide し、3000 px/s の fling の距離（±15%）で止まる。scroll は pointer の event を作らない。上端の先へ引くと指より少なく伸び、ばねで 0 に戻る。tap は同じ点の press・release。glide を止めた tap は何も押さない。長押しは離すまで何も作らず、離すと右の button の click（1 回）。長押しの後の drag は押した点の左の press と motion を作り、context menu を作らず、items は動かない。cancel は押した button を離す。sidebar の drag は sidebar だけを動かす。wheel の scroll と別の folder は glide を止める |
| mutation（scratchpad の script、6 個: catch の tap、長押し、外の scroll の検出、area の判定、scroll の範囲、fling の速度） | 6 個すべてが FAIL（検出） |
| `run-scroll.sh`・`run-motion.sh`・`run-termtouch.sh`・`run-pdftouch.sh`・`run-notestouch.sh`（motion.c を変えたので libkeiland を使う試験） | それぞれ ok（88・426009・20・33・30 checks） |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/files build/amd64/bin/wayland build/amd64/bin/touchinject build/amd64/dynamic/libkeiland.so`（`-Werror`） | rc=0、`warning:`・`error:` 0 |
| [p010-guest.sh](../tests/p010-guest.sh)（worktree の `build/ws081-main-pen.img`（main の pen の image の複写）の guest に、compositor・Files・touchinject・libkeiland 等を SSH で置く。Files は /tmp/ftest（folder 2 つと 150 個の file）を 900x620 で表示） | **PASS**（18 項目の ok） |
| 同上の中身 | tap で file0 を選ぶ（`ZFILES SELECT count=1`）。file1 の長押しを離すと context menu（`ZFILES CONTEXT-MENU open`、zdesktop の `ZWL MENU context client=1`）。file2 の長押しから folder beta へ運ぶと移動（`DRAG target kind=folder`、`drop operation=move`、beta に file2.txt）。長押しから窓の外へ運ぶと zdesktop の drag and drop（`ZFILES DND start`、`ZWL TOUCH drag start`、desktop の上で離すと `drag cancel reason=release`、Files は `dropped=0`）。folder alpha の二回の tap で開き、titlebar の Back で戻る。上への flick（1471 px/s）で items が進み、離した後に 265 px glide した。compositor の ERROR/FAILED 0 |
| `plan/tools/style-check.py`・`plan/ws081/temp/style-extra.py`（touch.c・touch.h・main.c・window.c・window.h・host-filestouch.c） | この Phase の行の指摘 0。main.c の `variable-comment`（一つの comment でまとめた既存の変数の群）と window.c の 2 つの `joined-check` は既存の行で、この Phase では変えていない |

画面（QEMU）は worktree の `build/ws081-shots/ws081-p010-20260929-*.png` です（元は `build/ws081-p010-guest/`）。

- `-start.png`: 開いた所
- `-context.png`: file1 の長押しの context menu
- `-out.png`: 窓の外への drag and drop の後
- `-alpha.png`: 二回の tap で開いた alpha
- `-flicked.png`: flick と glide の後（file9 の行が上端）

## 未実施・制限

- 実機は未確認（p007）。
- 二本指の拡大（item の大きさ）は入れていない。
- list view・column の表示の指の操作は、grid と同じ経路（hit の判定と scroll）で、guest では grid だけを試験した。
- rename の field の中の文字の選択は、左の button の pointer として扱う（指の選択の handle は無い）。
- boot test は main に依頼する（subagent は image を build しない）。
