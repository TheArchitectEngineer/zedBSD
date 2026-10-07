<!-- awesome-plan project=zedbsd record=ws142-p009 -->

# ws142-p009: touchpad の gesture の体系（TOP2、全画面の BOTTOM2、WiseView・preview の 1 swipe 1 つ）

Status: cleared（2026-10-08 Q1 の判定: p010-guest.sh が T1-342（2026-10-07、status 0）と T1-359（status 0）で PASS）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q781 / q781-i01
Design: [ws142-p007](../phase007/phase.md) §2
Related: [BUG-215](../../bugs/BUG-215.md)・[BUG-216](../../bugs/BUG-216.md)・[BUG-224](../../bugs/BUG-224.md)・[BUG-228](../../bugs/BUG-228.md)・[BUG-209](../../bugs/BUG-209.md)（4 つの仮定は 2026-10-06 ユーザーが推奨どおりに決定、今の実装のまま）

## 範囲と受け入れ（p007 §2 の表）

| gesture | 状態 | 動作（実装） |
| --- | --- | --- |
| 下端から 2 本指で上（BOTTOM2） | 窓・最大化 | WiseView（従来） |
| BOTTOM2 | 全画面 | 全画面 → 最大化（`gesture_fullscreen`、mode は DOCKED、`ZWL GLASS fullscreen-leave … via=bottom2`） |
| 上端から 2 本指で下（TOP2、新規） | 最大化 | 最大化 → 窓（`gesture_undock`、`ZWL GLASS undock … via=top2`、mode は WINDOWED） |
| TOP2 | 全画面 | 全画面 → 最大化（1 段ずつ、via=top2） |
| WiseView の中で 2 本指の左・右 | WiseView | 1 回の swipe で 1 つ（`ZWL WISEVIEW select step=+1|-1 via=swipe` と `current surface=`） |
| WiseView の中で 2 本指の下 | WiseView | 選んだ窓で確定（`ZWL WISEVIEW select surface= via=swipe`、`zwl_glass_switch_to` で mode に合わせる） |
| 3 本指の tap（TAP3） | — | switcher（従来。今の app から、BUG-209 の決定どおり） |
| preview（switcher）の中で 2 本指の左・右 | switcher | 1 回の swipe で 1 つ（`ZWL SWITCH step … via=pad`、連続の travel をやめた） |
| preview の中で 2 本指の下・tap | switcher | 確定（`commit … via=pad-swipe`・`via=pad`） |

- 「1 回の swipe」: 指が置かれてから離れるまで。8 mm（`ZWL_SWIPE_STEP_UM`）を超えた時に一番動いた向きで 1 回だけ決め、離すまで追加しない。WiseView・switcher の間は 2 本指の scroll を client に渡さない。
- 端から始めた 2 本指（LEFT2・RIGHT2・TOP2）も WiseView・switcher の間は swipe として扱う（右・左・下）。
- WiseView の題の文字「Wiseview - N windows」は描かない。案内は「Swipe left or right to choose · Swipe down to open」（ja: 左右にスワイプして選ぶ · 下にスワイプして開く）。

## 実装（2026-10-06）

| 所 | 内容 |
| --- | --- |
| `touchpad.h`・`touchpad.c` | `ZWL_TOUCHPAD_GESTURE_TOP2`（上端の帯 `EDGE_TOP`、下向き、travel は下が正）、`ZWL_TOUCHPAD_GESTURE_SWIPE2` の END（2 本指で scroll した touch が離れた時に 1 回: 全部離れた時・1 本離れた時・device が消えた時、scroll の後）、`ZWL_TOUCHPAD_NOTCH_UM`、`scrolled` |
| `swipe.c`・`swipe.h`（新、純粋） | `zwl_swipe_take`（8 mm で 1 回だけ左・右・下・上を決める）、`zwl_swipe_end`、`zwl_swipe_name` |
| `shell.c` | `zwl_glass_pad_scroll`（switcher・WiseView の間の 2 本指の scroll → swipe、`ZWL SWIPE dir via=pad`）、`wiseview_pad_swipe`・`wiseview_choose`・`wiseview_move`（key の処理から切り出し）、`gesture_fullscreen`・`gesture_undock`・`gesture_as_swipe`、`zwl_glass_gesture` に SWIPE2 の END・TOP2・全画面・WiseView/switcher の端の swipe、gesture の名前 top2・swipe2、WiseView の題の削除と案内の文 |
| `switcher.c`・`.h`・`switcher-shell.c` | `zwl_switcher_travel`（12 mm ごとに連続）を削除、`zwl_switch_pad_swipe`（右 +1、左 -1、下で確定） |
| `input.c` | SCROLL を `zwl_glass_pad_scroll` へ |
| `zwl.h` | `pad_swipe`、宣言 |
| `Makefile*` | `swipe.c` |
| `userland/desktop/locale/ja/wayland.tr` | `tr.py update`、新しい案内の訳、使わなくなった 2 つは comment で残る |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor・Linux の Keiland の build | 成功、warning 0 |
| `run-host-gesture.sh` | 62 checks ok（新: TOP2、上端から外向き・1 本だけ上端は gesture でない、scroll の touch の END は 1 回で scroll の後、1 本離れた時に END で 2 回目なし、1 本指は END なし） |
| `run-host-swipe.sh`（ASan・UBSan でも） | 15 checks ok |
| `run-host-switcher.sh` | 31 checks ok（travel の試験を step に） |
| `run-host-apps.sh`・`run-host-super-tap.sh` | ok |
| `tr.py check`（ja/wayland.tr） | 116 entries, 116 translated, 0 problems |
| style-check（変えた C の全部） | 指摘 0 |
| keiland-os-boundary | 既存の FAIL だけ（swipe.c は 3 つの Makefile に揃う） |
| QEMU | 未実施。p010 で T1 に（touchinject の pad で TOP2・WiseView の swipe・全画面の BOTTOM2） |
| 実機 5330 | 未実施（ユーザーの UAT: 感触、8 mm の閾値） |

## 2026-10-06 追加（P2）

- BUG-216 の「画面に Wiseview の文字を出さない」に、bar の題の場所の「Wiseview」の文字（`draw_system_bar`）も含めて消した（WiseView の画面の上の題は p009 で消し済み）。catalog の `Wiseview` は使わなくなった（comment で残る）。zedBSD の build warning 0、tr.py check 0 problems。
