<!-- awesome-plan project=zedbsd record=ws081-p006 -->

# ws081-p006: ブラウザの慣性の scroll と touch の入力（browser の shell）

1 回目の試み（2026-09-29）は、下の「確かめたこと」の時点で API が無かったので uncleared で止めた。2 回目は main の判断（同日）で API を足し、cleared にした（下の「2 回目」）。

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。2 回目の試み。main の判断で view の API（`browser.h` version 2）を足し、shell に wl_touch・慣性の scroll・rubber band・tap と長押しの click を入れた。host 試験と QEMU の guest 試験（main の pen の image）。実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29 夕「p010 → p015 → p006」、同日「要る API の 1〜3 を WS074 の view の側に足し、shell で p006 を実装」）。Awesome Plan の Queue の item ではない
Resume point: なし（入れ子の scroller の `scroll_by`、頁の script への TouchEvent（上の 4・5）は今回入れない。WS074 の計画に合わせて別の Phase）
<!-- awesome-plan-current:end -->

## 目的

Files・Terminal・PDF Viewer・Notes と同じ手触りで、ブラウザの頁を指で scroll する（慣性、端の rubber band、catch）。tap は click、長押しは右の button。
touch の処理は browser の shell（`/bin/browser` の窓の側）に置き、libbrowser・js・layout の内部は変えない（main の指示、2026-09-29）。

## 確かめたこと（main ff6e92ee の後の 36509f70、2026-09-29）

`include/libc/browser.h`（`BROWSER_API_VERSION` 1）と `userland/desktop/browser/view/view.c` を読んだ。

- scroll に関する公開の API は `browser_view_scroll_y`（読むだけ）、`browser_view_document_height`、`browser_view_wheel`（delta の px）だけ。
- engine の scroll は頁の根の縦だけ（`view->scroll_y`、`view_scroll` が `[0, 文書の高さ − view の高さ]` に抑える）。
  - 位置を設定する公開の API は無い。
  - 端を越えた分（overscroll）を描く手段は無い。
  - 入れ子の scroller（`overflow: auto` の要素）も無い。
- `browser_view_wheel` は頁の script に WheelEvent を先に渡し、cancel されなければ scroll する。
  - shell が慣性を毎 frame の wheel の delta として送ると、glide の間ずっと script に偽の WheelEvent が届き、script が止めることもできる。
  - 端での rubber band も、delta を使い切ったかの返りも無いので作れない。
  - design §6・§8 の懸念のとおり。

よって design §8 の依存（p006 は WS074 の `browser.h` の変更に依存）が満たされていない。

## 要る API（WS074 への依頼の案。名前は WS074 が決めてよい）

最小（これで p006 を始められる）:

1. **scroll の位置の設定**: `int browser_view_scroll_to(struct browser_view *view, double x, double y);`
   - 頁の根の scroll を px で置く。WheelEvent は出さず、頁には `scroll` event だけを出す。
   - 範囲の外は範囲に抑える（下の 3 の overscroll は別）。redraw の callback を呼ぶ。
2. **scroll の範囲**: `int browser_view_scroll_range(struct browser_view *view, double *largest_x, double *largest_y);`
   - 今の layout での最大の scroll（文書の大きさ − view の大きさ、0 以上）。layout が要れば行う。
   - `browser_view_document_height` と view の高さから shell でも計算できるので、無くてもよい。ただし横の scroll と、layout の遅延（画像の到着で高さが変わる）を engine が一か所で扱える。
3. **overscroll の描画**: `int browser_view_set_overscroll(struct browser_view *view, double dx, double dy);`
   - scroll の位置を変えずに、内容を dx・dy px ずらして描き、ずらした隙間は頁の背景色で埋める。
   - rubber band の見た目（端を越えて引く・ばねで戻る）は shell の scroller が計算し、毎 frame これで渡す。

あるとよい（後の Phase でもよい）:

4. **点の下の scroller と、使い切ったかの返り**: `int browser_view_scroll_by(struct browser_view *view, float x, float y, double dx, double dy, double *used_x, double *used_y);`
   - 指の下の最も内側の scroll できる要素から親へ順に scroll する（scroll chaining）。
   - 使った量を返し、余りを shell が overscroll にする。`overflow: auto` の入れ子の scroller が engine に入った時に要る。
5. **頁の script への touch**: `browser_view_touch(view, id, kind, x, y, modifiers)` で TouchEvent・PointerEvent（`pointerType: "touch"`）を渡す。
   - `preventDefault`・`touch-action` の結果を返す（cancel された touch は shell が scroll しない）。
   - これが無い間、shell は tap を `browser_view_pointer_button` の click として渡す（p010〜p012 と同じ）。

shell の側（WS081 p006 で行うこと）: wl_touch の bind、libkeiland の scroller・gesture。
- 一本指（二本指も重心で）の drag で `browser_view_scroll_to`、端の先は `browser_view_set_overscroll`。
- tap は primary の click、長押しは secondary の button。
- key・wheel・link の移動などで scroll が外から変わったら、指はそれを引き取って glide を止める（`browser_view_scroll_y` の変化で知る）。

## 確認

実装に入っていないので、試験はありません。読んだ file: `include/libc/browser.h`、`userland/desktop/browser/view/view.c`（`view_scroll`・`view_clamp_scroll`・`browser_view_wheel`）、
`userland/desktop/browser/shell/`（file の一覧）、`plan/ws081/design.md` §6・§8、`plan/ws074/ws.md`。

## 要る判断（main へ）

- 上の 1〜3 を WS074 に依頼する（main が WS074 に依頼する、2026-09-29 の指示）。4・5 は WS074 の計画（入れ子の scroller・TouchEvent）と合わせて決める。
- 代わりの案（意見としては取らない）: 今の `browser_view_wheel` だけで慣性を作る。rubber band は無く、glide の間に script へ偽の WheelEvent が出続ける。

## 2 回目（2026-09-29）: main の判断

main の判断: 要る API の 1〜3 を、この Phase で WS074 の view の側に足す。
- 足す場所は `include/libc/browser.h` と `userland/desktop/browser/view/` で、`BROWSER_API_VERSION` を上げる。
- WheelEvent は出さず、範囲に抑え、redraw の callback を呼ぶ。overscroll は内容をずらし、隙間は頁の背景色で埋める。
- js・layout・loader の内部には触れない。4・5 は入れない。WS074 の plan は変えない（main が WS074 に知らせる）。

### 足した API（WS074 の側、`BROWSER_API_VERSION` 1 → 2）

| API | 内容 |
| --- | --- |
| `int browser_view_scroll_to(view, x, y)` | 頁の根の scroll を px で置く。layout を最新にしてから `[0, 最大]` に抑える（`view_clamp_scroll`）。変わったら redraw の callback を呼ぶ。WheelEvent は出さない。頁が無ければ ENOENT。横の scroll は engine に無いので x は使わない |
| `int browser_view_scroll_range(view, &largest_x, &largest_y)` | 今の layout の文書の高さ − view の高さ（0 以上）。横は 0。頁が無ければ ENOENT と 0 |
| `int browser_view_set_overscroll(view, dx, dy)` | scroll を変えずに、描く時の scroll を `scroll_y − dy` にする。内容は dy だけ下へずれ、隙間は paint がはじめに塗る canvas の色（頁の背景）になる。±view の高さに抑える。変わったら redraw。新しい頁では 0 に戻る。pointer の位置の計算には入れない（一時的な見た目）。dx は使わない |

- 実装は `view.c` だけ（`struct browser_view` に `overscroll_y`、描画の 4 か所で `view_drawn_scroll`）。paint・layout・js・loader・page は変えていない。
- 頁への `scroll` event は出していない。engine は今、wheel・key の scroll でも `scroll` event を出さない（page の側に出す手段が無い）。
  出すには page・js の変更が要るので、今回の範囲の外（WS074 へ）。

### shell の側（WS081）

| file | 内容 |
| --- | --- |
| `userland/desktop/browser/shell/touch.h`・`touch.c`（新） | Wayland と engine に依存しない（host で試験できる）。libkeiland の gesture・scroller。`shell_touch_layout`（頁の番号、scroll、範囲、view の高さ。外で変わった scroll・別の頁は引き取り、glide を止め、伸びを 0 に）、`_event`、`_tick`、`_scroll`（範囲の中の scroll と、範囲の外の分を overscroll に）、`_take_pointer`、`_clock` |
| 同上の動き | 一本指・二本指（重心）の drag で scroll、flick で glide、端の先は rubber band（scroller の f⁻¹）で伸びてばねで戻る。glide を tap で catch（click しない）。tap は primary の click（motion・press・release）。長押しを動かさずに離すと secondary の click。長押しの後に動かすと scroll（click なし）。compositor の cancel は glide なし。`ZBROWSER TOUCH …` の log（stdout） |
| `shell/window.c`・`internal.h` | seat の TOUCH の capability で `wl_touch` を bind。down・up・motion・cancel を touch の queue（256）に入れる。queue があれば poll を待たない。閉じる時に `wl_touch_destroy` |
| `shell/shell.c` | 毎回の round（`shell_touch_round`、指が無く glide も無ければ何もしない）で、`browser_view_scroll_range`・`browser_view_scroll_y` を指に渡し、event を渡し、tick し、`browser_view_scroll_to`・`browser_view_set_overscroll` で置き、作った pointer の event を `browser_view_pointer_move`・`_button` で渡す。tick の待ちを loop の timeout に入れる。`committed` の callback で頁の番号を進める |
| `userland/desktop/browser/Makefile` | `shell/touch.c` |

### 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain`・`asan`（engine と WS074 の host 試験） | rc=0、warning 0 |
| WS074 の回帰（plain）: `golden-dumps.sh dom style layout paint`、host-view・host-form・host-link・host-position・host-text | golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20（全て 0 failed） |
| 同上（ASan、`ASAN_OPTIONS=detect_stack_use_after_return=0`） | golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19 |
| [run-browser-scroll.sh](../tests/run-browser-scroll.sh)（[host-browser-scroll.c](../tests/host-browser-scroll.c)、[pages/scroll.html](../tests/pages/scroll.html)。engine の host の object と link）、plain と ASan | 25 checks、0 failed |
| 同上の中身 | 頁が無いと ENOENT。範囲は 3200 − 300 = 2900。`scroll_to(1234.5)` はその位置で、redraw を呼び、WheelEvent は出ない（比較の wheel は出る）。同じ位置は redraw しない。上・下の外は端に抑える。端の青い block が描かれる。overscroll 50 で上 50 px が canvas の緑、赤い block が 50 px 下へ、scroll は不変。末尾で −40 は下に canvas。上限は view の高さ。新しい頁では 0 |
| [run-browsertouch.sh](../tests/run-browsertouch.sh)（[host-browsertouch.c](../tests/host-browsertouch.c)、touch.c と libkeiland、`-Wconversion -Werror`）、plain と ASan | 21 checks、ok |
| 同上の中身 | flick（3000 px/s）で進み、glide して fling の距離（±15%）で止まり、伸び・click は無い。上端で 200 px 引くと指より少なく伸び（30〜190）、離すとばねで 0。末尾で逆向きに伸び、戻る。末尾への fling は伸びてから末尾で止まる。tap は primary の click（その点）で scroll しない。glide を止めた tap は click しない。長押しを離すと secondary の click、長押しの後の drag は scroll で click なし。二本指は重心で scroll。外で置かれた scroll・同じ scroll の別の頁は glide を止め、指があれば新しい scroll から drag を続ける。cancel は glide なし |
| mutation（scratchpad の script、10 個: overscroll の計算、catch の tap、長押し、長押しの後の drag、外の scroll の検出、頁の番号、fling の速度、repress、範囲の上限 ほか） | 10 個すべてが FAIL（頁の番号の mutation は、同じ scroll の別の頁の試験を足して検出） |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/browser build/amd64/dynamic/libbrowser.so`（`-Werror`） | rc=0、`warning:`・`error:` 0 |
| [p006-guest.sh](../tests/p006-guest.sh)（worktree の `build/ws081-main-pen.img` の guest に compositor・browser・libbrowser・libgif-compat・libkeiland 等・touchinject と [pages/touch.html](../tests/pages/touch.html) を SSH で置く。browser 900x640） | **PASS**（ok 14 項目） |
| 同上の中身 | 頁の上端に赤い block（画面の pixel）。tap で `mousedown button=0`・`click button=0`。長押しを離すと `ZBROWSER TOUCH context`・`mousedown button=2`。上への flick（1471 px/s）で離した後に 512 px glide、頁の wheel event は 0。下への flick で上端に戻る（`rest scroll=0`）。上端で 200 px 引いて保つと、画面の上に canvas の緑が見え（stretched）、離すと `rest scroll=0` で赤い block が上端に戻る。compositor の ERROR/FAILED 0、`ZBROWSER ERROR` 0 |
| `plan/tools/style-check.py`・`plan/ws081/temp/style-extra.py`（view.c・browser.h・shell の touch.c・touch.h・shell.c・window.c・internal.h、host 試験 2 つ） | この Phase の行の指摘 0。window.c:179 の joined-check、view.c:519・554 の return-call は既存の行 |

画面（QEMU）は worktree の `build/ws081-shots/ws081-p006-20260929-*.png` です（元は `build/ws081-p006-guest/`）。

- `-start.png`: 開いた所（上端に赤い block）
- `-flicked.png`: flick と glide の後
- `-stretched.png`: 上端の先へ引いて保った所（上に canvas の緑が約 100 px、赤い block が下へずれている）
- `-sprung.png`: 離してばねで戻った所

### 未実施・制限

- 実機は未確認（p007）。
- WS074 の guest の試験（browser-p056 等、browser の image）は流していない。shell の今までの入力の経路は変えていない。回帰は host の試験だけ。
- 頁への `scroll` event は出ない（engine の既存の制限。page・js の変更が要る）。
- 入れ子の scroller（`overflow: auto`）と scroll chaining（上の 4）、TouchEvent・PointerEvent・`touch-action`（上の 5）は入れていない。
  指は常に頁の根を scroll し、頁の script は指の tap を mouse の event として受ける。
- 横の scroll は engine に無い（x・dx は使わない）。二本指の拡大（頁の zoom）は無い。
- overscroll の間の pointer の位置は、ずらした見た目を考えない（一時的な見た目なので）。
- guest の log では、速い flick で離した時の scroll が指の動きより小さい（drag の resampling の遅れ。fling が残りを運ぶ）。逆向きの flick の始まりに 2 px 戻る記録が 1 件ある（`drag scroll=555` → `release scroll=557`）。手触りの確認は p007 の実機で行う。
- boot test は main に依頼する（subagent は image を build しない）。
