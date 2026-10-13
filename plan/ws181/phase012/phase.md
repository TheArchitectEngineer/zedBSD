<!-- awesome-plan project=zedbsd record=ws181-p012 -->
# ws181-p012: touchpad の上端・下端の 2 本指の入れ替え（下端 → App Home、上端 → Wiseview）

Status: in-progress（実機の感触の UAT の分は OK（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）。1・2 回目は T1-559・T1-560（QEMU）PASS。3 回目（App Home の page の送りの量を半分に）の QEMU（T1）が未実施で残り。touchpad の送りの量は判断待ちで変えていない）
Disposition: normal
Parent: [WS181](../ws.md)
Queue: Q1 の割り当て（2026-10-13 ユーザーの指示）

## 由来（ユーザー、2026-10-13、原文）

> タッチパネルの下端からスワイプするとApp Homeが出ますが、トラックパッドの下端からスワイプするとウィンドウ一覧が出ますね。トラックパッドの下端からスワイプすると、App Homeが出るようにしてください。同様に、トラックパッドの上端からスワイプすると、App Homeが出ているので、これはウィンドウ一覧に変更してください。

## 置き換える決定

- [p008](../phase008/phase.md) の範囲 3（2026-10-07 の UAT「タッチパッドの上端からのスワイプダウン … ホーム画面への遷移に割り当ててほしい」、TOP2 → App Home）を、この指示で置き換える。touchpad の端の割り当ては touch panel の端と同じになる: 下端から上 → App Home、上端から下 → Wiseview（窓の一覧）。
- [p009](../phase009/phase.md) の範囲 3（App Home の上で下端から 2 本指で上 → desktop に戻る）は、この指示と矛盾しないので残す（下端の 2 本指は App Home の開閉の toggle になる）。→ 同じ日の 2 回目の指示（下の「追加」）で、下へ払って閉じる形に置き換えた。
- touch panel（指で画面）の gesture は変えない。

## 割り当て（実装後）

| 今の画面 | touchpad の 2 本指 | 前 | 今 |
| --- | --- | --- | --- |
| desktop（窓・docked） | 下端から上（BOTTOM2） | Wiseview が指に付いて開く | **App Home** が指に付いて開く（travel / 40 mm、離した時 0.30 以上か flick 100 mm/s で開く、足りなければ戻る。`KWL HOME pad swipe closing=0`・`KWL HOME open via=pad`・`KWL HOME pad back`） |
| desktop | 上端から下（TOP2） | App Home | **Wiseview** が指に付いて開く（travel / 40 mm、0.35 以上か flick で開く。`KWL WISEVIEW gesture via=pad`・`opening`・`cancel`） |
| App Home | 下端から上 | desktop に戻る（p009） | 同じ（`KWL HOME pad swipe closing=1`・`KWL HOME close via=pad`・`pad stays`）→ **2 回目で何もしないに変えた**（下の「追加」） |
| App Home | 上端の帯の外から 2 本指で下へ | page（scroll） | **2 回目で追加**: App Home が指に付いて閉じる（DOWN2） |
| App Home | 上端から下 | 何もしない | App Home をすぐ閉じて Wiseview が指に付いて開く（touch panel の上端と同じ。`KWL HOME close via=pad-top`） |
| Wiseview | 下端から上 | 何もしない | Wiseview をすぐ閉じて App Home が指に付いて開く（touch panel の Wiseview の下端の press と同じ。`KWL WISEVIEW close via=pad-bottom`） |
| Wiseview | 上端から下 | 今の tile を選ぶ | 同じ |
| 全画面の窓 | 下端から上・上端から下 | docked にする（BUG-228） | 同じ |
| どこでも | 3 本指で上（UP3） | Wiseview | 同じ |
| 電源の dialog | 上端から下 | 取り消す | 同じ |

touch panel の下端の swipe（`home.c` の `kwl_home_edge_press`・`home_rise_release`）は 360 px の 0.30 で決まり、速さの判定は無い。pad の版は今までの pad の規則（40 mm、0.30、flick 100 mm/s）を変えない。

## 実装（2026-10-13 P4）

- `shell.c` の `kwl_glass_gesture`: BOTTOM2 → `kwl_home_pad`（開く）、TOP2・UP3 → 新しい `gesture_wiseview_begin`（前の BOTTOM2・UP3 の case の中身を関数にした）。App Home の上の TOP2 → `kwl_home_close_now(server, "pad-top")` と `gesture_wiseview_begin`。Wiseview が出ている時（その開く gesture の最中でない）の BOTTOM2 → 新しい `wiseview_close_now(server, "pad-bottom")` と `kwl_home_pad`。`wiseview_close_now` は `wiseview_button` の下端の press（`via=bottom-edge`）と共用。
- `home.c`・`kwl.h`・`touchpad.c`・`touchpad.h` は注記だけ（`kwl_home_pad` の開く・閉じるの判定は始まりに Home が出ているかで決まり、変えていない）。`touchpad.c` の認識（BOTTOM2・TOP2 の判定）は変えていない。

## 試験の追従

- `plan/ws142/tests/p003-guest.sh`: 1.・2.（BOTTOM2 → App Home、`home-pad.png`、短い・遅いのは `pad back`）、5.（UP3 の数え直し）、7c.（TOP2 → Wiseview、`wiseview-pad.png`、短いのは `cancel`）、新しい 7d.（Home → TOP2 → Wiseview → BOTTOM2 → Home → Esc）、8.（全画面の上で Home が開かない）。
- `plan/ws142/tests/p010-guest.sh`: 2.（BOTTOM2 → App Home → Esc）、6.（TOP2 → Wiseview）。
- `plan/ws181/tests/p009-guest.sh`: 変更なし（App Home の上の BOTTOM2 で閉じる、意味は同じ）。
- `tests/scenarios/desktop/touchpad/preview-swipe-step.md`（3. を下端に、3b. の行き来を追加）・`wiseview-swipe.md`（1. を上端に）。
- host `plan/ws142/tests/host-gesture.c`: touchpad の認識だけの試験で、変更なし。
- 削除する試験: なし。

## 確認

| 確認 | 結果 |
| --- | --- |
| amd64 の compositor（`make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk -o <p4>/build/amd64/sysroot/.zedbsd-sysroot-complete build/amd64/bin/wayland`） | exit 0、warning 0 |
| rpi4 の compositor（`make -j8 ZEDBSD_CONFIG=config/ci/config-rpi4.mk -o <p4>/build/arm64/sysroot/.zedbsd-sysroot-complete build/arm64/bin/wayland`） | exit 0、warning 0 |
| Linux の Keiland（`make -j8 keiland-linux`） | exit 0、warning 0 |
| host `plan/ws142/tests/run-host-gesture.sh build/p4-host-gesture` | ok（82 checks） |
| `sh -n` p003-guest.sh・p010-guest.sh、`git diff --check` | ok |
| QEMU（p003-guest.sh・p010-guest.sh・p009-guest.sh） | 未実施（T1） |
| 実機（5320・5330 の touchpad の感触） | 未実施（ユーザーの UAT） |

## 追加（2026-10-13 の 2 回目）: App Home を touchpad の 2 本指で下へ払って閉じる

### 由来

前の報告の問い「App Home を閉じる pad の向きを touch panel のように下へ払う形に変えるか」へのユーザーの答え（2026-10-13、原文）:

> ・トラックパッドも下へ払って閉じる形に変えます。

### 判断（P4）

1. **上端の帯からの下（TOP2）と帯の外からの下（新しい DOWN2）を分ける**（既定の案のまま）。App Home の上の TOP2 は今の「Home をすぐ閉じて Wiseview が指に付いて開く」を残す。上端の帯（6 mm）の外から始まる 2 本指の下向きは App Home を閉じる。下端・左右の端の帯から下へ動かすのも DOWN2（帯の外と同じ扱い）。
2. **touchpad.c に DOWN2 を足した。ただし shell が頼んでいる間だけ作る**。touchpad.c には帯の外の 2 本指の下向きの gesture が無く、帯の外の 2 本指はすぐ scroll になる。DOWN2 を常に作ると、どこでも 2 本指で下へ scroll できなくなるので、`kwl_touchpad_set_down` で shell が App Home の出ている間だけ頼む形にした。touchpad の層は input thread で動くので、頼みは既存の設定の受け渡し（`struct kwl_input_settings`、`kwl_input_sync` が毎 pass で比べて変われば input thread を起こす）に `touchpad_down` を足して運ぶ。新しく付いた pad（attach）にも渡す。login・lock の画面では頼まない。
3. **App Home の中の 2 本指の scroll との衝突の解決**: 今の App Home は pad の 2 本指の scroll を wheel として受け、縦・横とも page をめくる（`kwl_home_axis`）。頼まれている間、端に触れない 2 本指も「すぐ scroll」にせず、他の端の gesture と同じく最初の 4 mm（`DECIDE_UM`）で決める: ほぼ下（下 ≥ 2 × 横）なら DOWN2、それ以外（横・上）はためた分から scroll（page をめくる、今まで通り）。下への 2 本指は App Home では page をめくらなくなる（page は横に並び、横の swipe と上の scroll でめくれる）。自然な解決と判断した: touch panel の Home の上でも下への drag は閉じ、横は page。
4. **閉じる決め方は pad の今の規則**（`kwl_home_pad` の閉じる向きをそのまま使う）: travel / 40 mm に指が付いて閉じ、離した時 0.30 以上か flick 100 mm/s（下向きの速さ）で閉じる（`KWL HOME close via=pad`）、足りなければ開いたまま（`KWL HOME pad stays`）。途中で上へ戻せば travel が減って開いたままに戻る。
5. **「App Home の上で下端から上 → 閉じる」（p009 の範囲 3）は外した**（既定の案）。下へ払う形に置き換えたので、App Home の上の BOTTOM2 は何もしない。touch panel でも Home の上の下端の swipe up は何もしない（`kwl_home_edge_press` は Home が閉じている時だけ）ので、touch panel と揃う。残すと「上へ払っても下へ払っても閉じる」になり、iOS と同じ「下端から上 = Home へ」の向きの意味が Home の上でだけ逆になるので、残す理由は無いと判断した。p009 の phase.md に置き換えの注記を足した。
6. 電源の dialog（App Home の Power Off から出る）が出ている時の DOWN2 は、TOP2 と同じく下への swipe として dialog を取り消す（`gesture_as_swipe`）。

### 割り当て（2 回目の後、App Home の上の pad）

| 2 本指 | 前（1 回目の後） | 今 |
| --- | --- | --- |
| 帯の外（または下端・左右の端）から下へ | page をめくる（scroll） | **App Home が指に付いて閉じる**（`KWL GESTURE kind=down2`・`KWL HOME pad swipe closing=1`・`KWL HOME close via=pad`・`pad stays`） |
| 上端の帯から下へ（TOP2） | Home をすぐ閉じて Wiseview | 同じ（`KWL HOME close via=pad-top`） |
| 下端の帯から上へ（BOTTOM2） | Home を閉じる（p009） | **何もしない**（`KWL GESTURE kind=bottom2` の行だけ） |
| 横へ・帯の外から上へ | page をめくる | 同じ（ただし最初の 4 mm で決まるまで待つ） |

desktop・Wiseview の上の pad、touch panel の gesture は変えていない。

### 実装

- `touchpad.h`・`touchpad.c`: `KWL_TOUCHPAD_GESTURE_DOWN2`（enum の末尾）、`struct kwl_touchpad` の `down2`、`kwl_touchpad_set_down`。`gesture_motion` は `down2` の間、端に触れない 2 本指もためて決め、TOP2 でない下向きを DOWN2 にする。`gesture_along` の DOWN2 は下が正。
- `input-thread.h`・`input-thread.c`: `struct kwl_input_settings` の `touchpad_down`、設定の世代が変わった時と attach の時に `kwl_touchpad_set_down`。
- `input.c` の `input_settings`: App Home が出ている（`kwl_home_progress` > 0 か `home_to` > 0）かつ login・lock でない時に `touchpad_down = 1`。
- `shell.c` の `kwl_glass_gesture`: DOWN2 は App Home が出ていれば `kwl_home_pad`（閉じる向き）、出ていなければ何もしない。App Home の上の BOTTOM2 は何もしない（前は `kwl_home_pad`）。`gesture_name` に `down2`、`gesture_as_swipe` の DOWN2 は下。
- `home.c`・`kwl.h`: 注記だけ（`kwl_home_pad` の判定は変えていない）。

### 試験の追従

- host `plan/ws142/tests/host-gesture.c`: 32.〜36.（頼まれた時の真ん中の下 20 mm が DOWN2 で付いて flick、遅いのは flick でない、上・横は scroll、上端からは TOP2 のまま、下端の帯から下は DOWN2、頼まれていない・頼みを外した時は下も scroll）。101 checks。
- `plan/ws181/tests/p009-guest.sh` の 3.: App Home の上で真ん中から下 30 mm → `kind=down2`・`closing=1`・`close via=pad`、下端の帯から下でも `closing=1`、短く遅いのは `pad stays`、下端から上（BOTTOM2）は何もしない（Esc で閉じる = 開いたまま）。`close via=pad` の照合を `close via=pad `（空白付き、`pad-top` と分ける）に。
- `plan/ws142/tests/p003-guest.sh` の新しい 7e.: App Home を下端から開き、横の 2 本指は gesture でなく開いたまま、真ん中から下で閉じる、desktop の上の同じ下は gesture でない。8. の数を 6・6 に。
- `plan/ws142/tests/p010-guest.sh`: App Home は Esc で閉じていて pad で閉じないので変更なし。
- AAT `tests/scenarios/desktop/touchpad/preview-swipe-step.md`: 3b. から「App Home の上で下端から上 → desktop」を外し、新しい 3c.（下へ払って閉じる・戻すと開いたまま・横は page・下端から上は何もしない）。
- 削除する試験: なし。

### 確認（2 回目）

| 確認 | 結果 |
| --- | --- |
| amd64 の compositor（`make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk -o <p4>/build/amd64/sysroot/.zedbsd-sysroot-complete build/amd64/bin/wayland`） | exit 0、warning 0 |
| rpi4 の compositor（`make -j8 ZEDBSD_CONFIG=config/ci/config-rpi4.mk -o <p4>/build/arm64/sysroot/.zedbsd-sysroot-complete build/arm64/bin/wayland`） | exit 0、warning 0 |
| Linux の Keiland（`make -j8 keiland-linux`） | exit 0、warning 0 |
| host `plan/ws142/tests/run-host-gesture.sh build/p4-host-gesture` | ok（101 checks） |
| host `plan/ws159/tests/run-host-touchpad.sh build/p4-host-touchpad` | ok（32 checks） |
| host `plan/ws213/tests/input-thread-host-test.sh build/p4-host-ws213`（設定の struct が変わったので） | PASS（131 checks、plain と ThreadSanitizer） |
| `sh -n` p003-guest.sh・p009-guest.sh、`git diff --check` | ok |
| QEMU（pen の image、p003-guest.sh・p010-guest.sh・p009-guest.sh） | 未実施（T1。1 回目の T1-559 と合わせて 1 回で流せる） |
| 実機（5320・5330 の touchpad の感触） | 未実施（ユーザーの UAT） |


## 追加（2026-10-13 の 3 回目）: App Home のページ送りに要る swipe の量を半分に

### 由来（ユーザーの UAT、2026-10-13、原文）

> ・App Homeで、ページ送りに必要なスワイプ量を半分にしてほしいです。

### 調べたこと（P4）

App Home の page が送られる経路は 3 つある。

1. **touch panel の指の swipe と mouse の drag**: 同じ経路（touch.c は App Home の上の指を pointer の左 button として shell に渡す）。`home.c` の `kwl_home_motion` が HOME_PAGE_START（10 px）動いた所で drag と決め（`home_page_follow`、横・下・上の向き）、`home_page_release` が離した時の横の移動 `dx` で決める: 前は出力の幅の 1/4 以上で次・前の page、足りなければ戻る。速さ（flick）の判定は無い（離した位置だけ）。
2. **touchpad の 2 本指の scroll**: `touchpad.c` の notch（`KWL_TOUCHPAD_NOTCH_UM` = 2.5 mm）ごとに `seat_axis` → `kwl_home_axis` が 1 page 送る。App Home が出ている間は DOWN2 を頼んでいるので（2 回目の実装）、2 本指は最初の 4 mm（`DECIDE_UM`、縦 + 横の和）で gesture か scroll かを決め、scroll と決まった時にためた 4 mm を流す → その場で 1 notch（1 page）。つまり 1 page めの送りに要る量は決めの 4 mm で、その後は 2.5 mm ごとにもう 1 page（1 回の swipe で何 page も進む）。page の送りだけの閾値は無い。
3. **mouse の wheel・PageUp/PageDown**: 1 notch・1 押しで 1 page。swipe の量ではない。

### 変えた定数

| 経路 | 定数 | 前 | 今 |
| --- | --- | --- | --- |
| touch panel の swipe・mouse の drag（`home.c` `home_page_release`） | 新しい `HOME_PAGE_TURN`（出力の幅をこれで割った横の移動で送る） | 幅 / 4（1280 px で 320 px、1920 px で 480 px） | **幅 / 8**（1280 px で 160 px、1920 px で 240 px） |

変えていない物: drag と決める HOME_PAGE_START（10 px、送りの量ではない）、下への drag で閉じる量（HOME_RISE_DISTANCE × HOME_THRESHOLD = 108 px）、touchpad の決めの 4 mm（`DECIDE_UM`、依頼の通り）、touchpad の notch 2.5 mm（`KWL_TOUCHPAD_NOTCH_UM`）、wheel の 1 notch 1 page。

### touchpad を変えなかった理由（判断が要る点として Q1 に返す）

- touchpad の 1 page めは 4 mm の決めで送られる。これを半分（2 mm）にするには `DECIDE_UM` を変えることになり、依頼で変えないとされた 4 mm そのもの（これは端の gesture 全部の決めでもある）。
- notch の 2.5 mm を App Home だけ半分にしても、1 page めは 4 mm の決めのまま変わらず、2 page め以降が 1.25 mm ごとに進む（今でも 1 回の swipe で何 page も進む）だけで、指摘の「必要な量」は減らない。notch は volume・tab・lock 画面・Wiseview の swipe と共用なので、共用の値は変えない。
- したがって touchpad は変えていない。実機で touchpad の page 送りも重いと感じるなら、(a) App Home の上でだけ「横が縦の 2 倍以上なら 2 mm で scroll と決める」、(b) 1 回の swipe で 1 page だけ送る（幅に対する比で指に付ける形）などの別の変更になり、ユーザーの判断が要る。

### 試験の追従

- `plan/ws181/tests/p009-guest.sh` の 4.: drag は 400 px（1280 の出力で前の 320 px・今の 160 px のどちらも超える）、1 枚めの右・最後の page の左は端で止まる。閾値に依らないので変更なし（pen の guest は Home が 1 page で、4. の送りは流れない）。
- `plan/ws142/tests/`・`tests/scenarios/desktop/`: page の送りの量に依る試験は無い（touchpad の preview-swipe-step.md の 3c. の「横へは page」は閉じないことを見るだけ）。変更なし。
- host 試験: `home_page_release` を host で動かす試験は無い（home.c の host の試験は無い）。定数 1 つの変更で、読んで確かめた。
- 削除する試験: なし。

### 確認（3 回目）

| 確認 | 結果 |
| --- | --- |
| amd64 の compositor（`make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk -o <p4>/build/amd64/sysroot/.zedbsd-sysroot-complete build/amd64/bin/wayland`） | exit 0、warning 0（home.c を build し直した） |
| rpi4 の compositor（`make -j8 ZEDBSD_CONFIG=config/ci/config-rpi4.mk -o <p4>/build/arm64/sysroot/.zedbsd-sysroot-complete build/arm64/bin/wayland`） | exit 0、warning 0 |
| Linux の Keiland（`make -j8 keiland-linux`） | exit 0、warning 0 |
| `git diff --check` | ok |
| QEMU（AAT の image、Home が 2 page: 幅 1280 の上で 220 px の横の drag で送られ、120 px では戻る） | 未実施（T1） |
| 実機（5320・5330 の touch panel の swipe の量） | 未実施（ユーザーの UAT） |
