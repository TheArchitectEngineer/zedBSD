<!-- awesome-plan project=zedbsd record=ws181-p012 -->
# ws181-p012: touchpad の上端・下端の 2 本指の入れ替え（下端 → App Home、上端 → Wiseview）

Status: in-progress（2026-10-13 P4、実装・build・host 試験まで。QEMU は T1、実機の感触はユーザーの UAT）
Disposition: normal
Parent: [WS181](../ws.md)
Queue: Q1 の割り当て（2026-10-13 ユーザーの指示）

## 由来（ユーザー、2026-10-13、原文）

> タッチパネルの下端からスワイプするとApp Homeが出ますが、トラックパッドの下端からスワイプするとウィンドウ一覧が出ますね。トラックパッドの下端からスワイプすると、App Homeが出るようにしてください。同様に、トラックパッドの上端からスワイプすると、App Homeが出ているので、これはウィンドウ一覧に変更してください。

## 置き換える決定

- [p008](../phase008/phase.md) の範囲 3（2026-10-07 の UAT「タッチパッドの上端からのスワイプダウン … ホーム画面への遷移に割り当ててほしい」、TOP2 → App Home）を、この指示で置き換える。touchpad の端の割り当ては touch panel の端と同じになる: 下端から上 → App Home、上端から下 → Wiseview（窓の一覧）。
- [p009](../phase009/phase.md) の範囲 3（App Home の上で下端から 2 本指で上 → desktop に戻る）は、この指示と矛盾しないので残す（下端の 2 本指は App Home の開閉の toggle になる）。
- touch panel（指で画面）の gesture は変えない。

## 割り当て（実装後）

| 今の画面 | touchpad の 2 本指 | 前 | 今 |
| --- | --- | --- | --- |
| desktop（窓・docked） | 下端から上（BOTTOM2） | Wiseview が指に付いて開く | **App Home** が指に付いて開く（travel / 40 mm、離した時 0.30 以上か flick 100 mm/s で開く、足りなければ戻る。`KWL HOME pad swipe closing=0`・`KWL HOME open via=pad`・`KWL HOME pad back`） |
| desktop | 上端から下（TOP2） | App Home | **Wiseview** が指に付いて開く（travel / 40 mm、0.35 以上か flick で開く。`KWL WISEVIEW gesture via=pad`・`opening`・`cancel`） |
| App Home | 下端から上 | desktop に戻る（p009） | 同じ（`KWL HOME pad swipe closing=1`・`KWL HOME close via=pad`・`pad stays`） |
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
