<!-- awesome-plan project=zedbsd record=ws181-p008 -->
# ws181-p008: UAT 2026-10-07 の 5 回目（App Home への遷移の effect、touchpad の端の 2 本指 swipe、上端の swipe down）

Status: in-progress（2026-10-07 q851-i01 P2: 実装・build・host 試験まで。QEMU は T1 へ、実機は 5320 のユーザーの UAT）
Disposition: normal
Parent: [WS181](../ws.md)
Queue: q851 / q851-i01（ユーザーの UAT 2026-10-07）

## 由来（2026-10-07 ユーザーの UAT、5320 実機）

「App Homeへ遷移するとき、現在のスライドのアニメだと、一時的な印象を与える。アプリのあるデスクトップが奥に消えていき、ホーム画面が奥から表れる、iOSと同様なエフェクトがいいと思う。」
「タッチパッドの左端、右端からの２本指スワイプについて、指が２本ともエッジにないと認識されていない。片方がエッジにあれば認識されるようにしたい。上端、下端も同様。」
「タッチパッドの上端からのスワイプダウンについて、現在はアプリ最大化解除に割り当てられているので、ホーム画面への遷移に割り当ててほしい。」

## 範囲

1. App Home への遷移: desktop（app の窓）が縮みながら奥へ消え、App Home が奥から大きくなって現れる（iOS と同じ）。戻る時は逆。今の slide を置き換える。
2. touchpad の端からの 2 本指 swipe（左・右・上・下）: 2 本のうち 1 本が端の帯にあれば端の swipe と認める。
3. touchpad の上端からの swipe down を App Home への遷移に（今は docked の解除）。docked の解除は他の操作（bar の 2 回 tap など）に残す。
4. 画面の左上からの右下への swipe は今 App Home だが、[WS184](../../ws184/ws.md) の左手デバイスの OSK に移るので、App Home の入口が上端の swipe down・Super・他に揃うよう整理する。

## 受け入れ

- build warning 0、host 試験、T1 の QEMU（ws181-guest.sh・p003-guest.sh の追従）、5320 でユーザーの UAT。

## 実装（2026-10-07 P2）

1. **遷移（iOS と同じ奥へ・奥から）**: `edge.c` に純関数 `kwl_edge_home_desktop`・`kwl_edge_home_content`（`struct kwl_edge_depth`）。desktop の層は progress に合わせて出力の真ん中を軸に 1 → 0.75 に縮み（`KWL_EDGE_HOME_DESKTOP_DEPTH`）、progress 0.2〜0.9 で不透明から透明へ（`KWL_EDGE_HOME_FADE_*`）。Home の中身（時計・床・icon・検索・頁の点）は 0.85 → 1 に大きくなる（`KWL_EDGE_HOME_CONTENT_DEPTH`）。暗い stage は全面のまま。閉じる時は同じ道を逆に。
   - `kwl.h` の layer に `layer_opacity` を足し、`glass.c` の `glass_shape_draw` で layer の shape の opacity に掛ける（0 の時は描かない）。`shell.c` の `window_layer` は 1 に戻す。`home.c` の `kwl_home_layer` は depth を返し（引数に opacity）、`kwl_home_draw` は中身を layer で描いて最後に `layer_on = 0`。今までの上へ出ていく動き（`HOME_SHADOW`）は削除。
2. **touchpad の端の 2 本指**: `touchpad.c` の `edges_of_fingers` を「全部の指が帯に」から「どれか 1 本が帯に」に（下・左・右・上）。判定の向き（内向き、ほぼその向き）は変えない。
3. **上端の 2 本指の下（TOP2）→ App Home**: `home.c` の新しい `kwl_home_pad`（指に付く: travel / 40 mm、離した時 0.30 以上か flick 100 mm/s で開く。log `KWL HOME pad swipe`・`KWL HOME open via=pad`・`KWL HOME pad back from=N`）。`shell.c` の `kwl_glass_gesture` は TOP2 の始まりと、その後の phase（`server->home_pad`）を Home へ。`gesture_undock`（TOP2 で docked を外す、BUG-224）は削除: docked の解除は bar の title の 2 回 click・restore の button・pull・三回 click に残る。全画面の上の TOP2（BUG-228: docked にする）は変えない。
4. **左上の角**: 左上の角・launcher から右下への drag で Home を開くのを削除（WS184 の左手デバイスに空ける）。launcher・角の click・tap は今どおり開く・閉じる。14 px 動いた press は click でなくなり、離しても何もしない（log `KWL HOME press slipped`）。App Home の入口は launcher の click・tap、Super、desktop の下端からの swipe up、touchpad の上端の 2 本指の下。

## 試験の追従（2026-10-07 P2）

- host: `plan/ws181/tests/host-edge.c`（depth の 10 例、`run-host-edge.sh` に `-lm`）、`plan/ws142/tests/host-gesture.c`（case 6・23: 1 本だけ端 → 今は BOTTOM2・TOP2、6b: 1 本だけ左の端 → LEFT2）。
- QEMU の試験（T1 が流す）: `plan/ws181/tests/ws181-guest.sh` の B7（半分で止めた `b7-home-half.png`、角の drag は何もしない、角の click で開閉）、`plan/ws142/tests/p003-guest.sh` の 7b（1 本だけ右の端）・7c（TOP2 → Home、`home-pad.png`、短い TOP2 は戻る）、`plan/ws142/tests/p010-guest.sh` の 2.（TOP2 → Home → Esc、bar の title の 2 回 click で floating）、`plan/ws079/tests/zdesktop-p010.sh`・`zdesktop-p013-touch.sh`（Home を角の drag → click・tap で開く）、`tests/scenarios/desktop/touchpad/preview-swipe-step.md` の 3.・5.。

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`） | 成功、warning 0（-Werror） |
| Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux`） | 成功、warning 0 |
| host `plan/ws181/tests/run-host-edge.sh` | PASS（checks=69 failures=0） |
| host `plan/ws142/tests/run-host-gesture.sh` | PASS（66 checks） |
| `plan/tools/keiland-linux/makefile-sync.sh` | wayland の不一致なし（preview の既存の FAIL は無関係） |
| QEMU | 未実施（T1） |
| 実機（5320） | 未実施（ユーザーの UAT） |
