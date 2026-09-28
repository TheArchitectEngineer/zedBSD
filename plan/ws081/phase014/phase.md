<!-- awesome-plan project=zedbsd record=ws081-p014 -->

# ws081-p014: 指の drag and drop（compositor、wl_touch の client）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。compositor の `data.c`・`touch.c` に指の drag and drop、Terminal の選択の文字の指の drag。QEMU の guest 試験（main の pen の image）。実機は未実施）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「判断 1 は (1): 新しい Phase として compositor の `data.c` に touch の drag and drop（start_drag を wl_touch の down の serial で受け、drag を指に付けて動かす、drop・cancel）を入れ、その後 p010。`data.c` と drag に要る compositor の file の変更を許可。Terminal の選択した文字の指の drag も同じ仕組みで動くなら同じ Phase で確かめる」）。Awesome Plan の Queue の item ではない
Resume point: なし（次は p010）
<!-- awesome-plan-current:end -->

## 設計

- `wl_data_device.start_drag` は、pointer の button が押されている時の他に、次の時も受けます: その client が wl_touch で聞いている指がまだ押されていて、その `wl_touch.down` の serial で頼まれた時。
  - compositor は down の serial を指ごとに覚えます（`touch_contact.down_serial`）。
- 受けたら、その指は drag の指になります（新しい route `ROUTE_DRAG`）。drag の仕組み（`data.c`）はすべて pointer の位置で動くので、次のように pointer を指に付けます。
  - 指の報告ごとに pointer をその指の位置へ動かし、`zwl_data_drag_motion` を呼びます。target の enter・motion・leave と、drag の icon（か badge）が指に付いて動きます。
  - 指の up で最後の位置へ動かしてから `zwl_data_drag_release`（drop か cancel）を呼びます。
  - touch screen が外れたら `zwl_data_drag_cancel` を呼びます。
- drag を始めた client には `wl_touch.cancel` を送ります。
  - その指はもう drag のものなので、client の gesture が指を持ったまま残らないようにします。
  - その client の他の指も、離すまでどこにも行きません（`cancel_client`。今までの `cancel_clients` の一つの client 版）。
  - Weston も drag の間はその touch の event を client に送りません。
- drag の終わりの後（Esc、source が消えた）も、指が離れるまでは `ROUTE_DRAG` のままです。motion と release は drag が無ければ何もしません。
- Terminal の選択の文字の指の drag（同じ仕組みの確かめ）
  - 長押しは、指の側（`touch.c`）では一つの「hold」の event にしました。main.c が次のように写します。
    - 選択の上: 一回の押し（今までの pointer の「範囲の中を押して動かすと文字を drag する」）。
    - 選択の外: 押す・離す・押す（p011 の単語の選択）。
  - 長押しの後に指を動かすと、Terminal はその押しの serial（= 指の down の serial）で start_drag を頼み、compositor が指を drag に渡します。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/wayland/touch.c`・`touch.h` | `ROUTE_DRAG`、`down_serial`、`zwl_touch_drag_start`（client と serial で指を探し、drag の指にし、client に cancel、pointer を指へ）、`cancel_client`。drag の指の move・lift・screen の外れ。log `ZWL TOUCH drag start …`・`ZWL TOUCH drag lift …`・`ZWL TOUCH cancel … reason=drag` |
| `userland/desktop/wayland/data.c` | `start_drag` は、button が無い時に `zwl_touch_drag_start` を試す。拒む log に serial を足した |
| `userland/desktop/terminal/touch.c`・`touch.h`・`main.c` | 長押しの `TERMINAL_TOUCH_HOLD` と、main.c でのその写し方（`main_touch_queue`）。log `ZTERM TOUCH hold on-selection=N` |
| `plan/ws081/tests/host-termtouch.c` | 長押しは hold の一つの event（20 checks のまま） |
| `plan/ws081/tests/p014-guest.sh`（新） | guest 試験（下） |

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/bin/wayland build/amd64/bin/terminal`（`-Werror`） | rc=0、`warning:`・`error:` 0 |
| `plan/ws081/tests/run-termtouch.sh` | `host-termtouch: ok (20 checks)` |
| [p014-guest.sh](../tests/p014-guest.sh)（main の pen の image の複写を起こし、compositor・terminal・touchinject・libkeiland を SSH で置く。Terminal 二つ: T1 は「dragme」を出す、T2 は shell。compositor は T2 を 48 px ずらして上に置く） | **PASS** |
| 同上の中身 | T1 の単語の長押しで `hold on-selection=0`・`ZTERM SELECT how=word … bytes=6`。選択の上の長押しで `hold on-selection=1`、指を T2 の見えている帯へ運ぶと `ZTERM DRAG start bytes=6`（T1）、`ZWL TOUCH cancel client=1 reason=drag`、`ZWL TOUCH drag start client=1 contact=0`、drag の enter・accept・action は T1 から T2 へ移り、指を離すと `ZWL DATA drag drop client=1 target=2`、T2 の `ZTERM DROP bytes=6`、T1 の `ZTERM DRAG done dropped=1`。compositor の ERROR/FAILED 0 |
| `plan/tools/style-check.py`・`style-extra.py`（compositor の touch.c・touch.h・data.c、terminal の touch.c・touch.h・main.c の足した部分、host-termtouch.c） | 指摘 0。`data.c` の 3 つの `joined-check` は既存の行で、この Phase では変えていない |

画面は worktree の `build/ws081-p014-guest/` にあります: `start.png`、`selected.png`、`dropped.png`（T1 の「dragme」が選ばれ、drop の後）。
log は同じ directory の `zdesktop-log.txt` と `terminals-log.txt` です。

## 未実施・制限

- 実機は未確認（p007）。
- pointer（mouse）で始めた drag の間に指が触れた場合や、指の drag の間に mouse が動いた場合、pointer は両方で動く（どちらも pointer の位置を使う）。試験していない。
- 二本目の指で drag を始める等の複数の指の組み合わせは試験していない（drag は一つの指）。
- boot test は main に依頼する（subagent は image を build しない）。
