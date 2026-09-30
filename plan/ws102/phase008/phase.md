<!-- awesome-plan project=zedbsd record=ws102-p008 -->

# ws102-p008: 手書きの面（L2 の (c)）

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施。(c) の 17 ms は、QEMU の frame の間隔では測れない（下）ので、「次の 1 frame で描く」を QEMU で確かめた）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §2.7 の手書きを作る。

- QWERTY の面から切り替える手書きの面。
- 線の記録と描画。
- stub の認識（決まった候補と「認識はまだ」）、候補の列。
- 後で認識の engine を差す interface。

受け入れ（design §3 の L2 の (c)）: 手書きの線が指に 1 frame（60 Hz で 17 ms）以内で追いつく。frame の時刻の log で確かめる。

## 実装

| file | 内容 |
| --- | --- |
| `keyboard-hand.c`（新規）・`keyboard.h` | ink の型: `zwl_hand_ink`（最大 64 画）・`zwl_hand_stroke`（1 画は最大 512 点）・`zwl_hand_point`。<br>ink の操作: `zwl_hand_clear`・`_begin`・`_add`（動かない点は持たない）・`_points`・`_bounds`。<br>**認識の interface**: `zwl_hand_recognize(ink, result)`。`zwl_hand_result` は候補（最大 4、ありそうな順）と note を持つ。今は stub で、ink があれば「あ・い・う」と note「認識はまだ」を返す。本物の engine はこの関数の中身を差し替える |
| `keyboard.c` | QWERTY の panel の帯に「手書き」の button を置き（閉じる key の左）、手書きの面では「ABC」で戻る。<br>面の配置は panel の中の相対の配置（p021 の縁に組み込む配置でもそのまま使える）。左が書く面（中央に淡い十字）、右の 300 px の列が上から note・候補 3 つ・消す と Del・space と Enter。<br>線: 押すと画が始まり、motion で点を足し（書く面の中に収める）、離すと画が終わる。最後の画から 600 ms で認識する。<br>描画: 各画の点の間に 2 px ごとに 4 px の丸い点を置き、濃い青の線にする。<br>候補を押すと p004 の送出で送り（かなは text-input の commit）、ink を消す。<br>log: `ZWL OSK hand on area=…`・`stroke-begin`・`stroke-end`・`recognize strokes= points= box= candidates= first= note=`・`key=`・`clear`。新しい点を描いた frame ごとに `hand frame lag_ms=（点の入力から描いた時まで）gap_ms=（前にそうした frame から）` |
| `Makefile` | `keyboard-hand.c` |

IME の file と seat.c は変えていない。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … bin/wayland …` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c keyboard-hand.c keyboard.h` | 0 件 |
| host | `sh plan/ws102/tests/host-keyboard.sh`（p008 の分を足した） | PASS。ink 無しでは候補も範囲も無い。画の点が保たれ、動かない点は持たない。2 画の範囲。stub の あ・い・う と note。1 画 512 点・64 画の上限 |
| guest: 手書き（新しい手順、pen image の複写） | `osk-guest.sh build/ws102-shots/p008-final install start pointer flick edges touch send close qwerty hand` | PASS（L1 と p006 の全ての手順を含む） |
| guest: 1920x1080 | `… install large` | PASS |
| 回帰 | WS079-p010、WS099 の C9（10 本）、boot test | PASS |

手順 hand で確かめたこと:

- 帯の button で手書きの面になった（`hand on area=18,526,938,256`）。
- pointer で 2 画（16 ms ごとに動かす）、注入の指で 1 画を書いた。3 画とも描かれた（`hand.png`）。
- 最後の画から 600 ms 後に stub が認識した: `recognize strokes=3 … candidates=3 first=あ note=認識はまだ`。
- 候補 あ を押すと、ime-probe に `PROBE TEXT text=あ` が届いた。
- 1 画書いてから「消す」を押すと、`hand clear` になり、認識は起きなかった。
- 帯の「ABC」で QWERTY に戻った。

## (c) 線の遅れの結果（QEMU の証拠）

新しい点を描いた frame の log の数値:

| 値 | 結果 |
| --- | --- |
| 点の入力から描いた frame まで（lag_ms） | 20〜53 ms（最初の点は 20〜22 ms） |
| 続けて描く時の frame の間隔（gap_ms） | 130〜143 ms（中央値 143 ms） |
| 入力の後の次の frame で描いたか | 9 frame とも、lag < その時の frame の間隔 |

- この guest（QEMU の Venus、host は lavapipe の software Vulkan）は、続けて描くと frame が 7 枚/秒前後しか出ない。WS099 の C5（QEMU の frame の間隔）が FAIL になっているのと同じ環境の制限。
- そのため、60 Hz を前提にした 17 ms は QEMU では測れない。QEMU で確かめたのは、**点が入力の後の最初の frame で描かれること**（1 frame 以内）。
- 17 ms の数値は、60 Hz の出る環境（実機の 5330、または Windows の上の QEMU）で、同じ log（`hand frame lag_ms`）で測る。L3 の p010 の計測、または L5 の実機で行う。
- 画の点の描画が frame を重くしているかは分けて測っていない。最初の点の frame の lag は 20 ms で、1 frame の描画はその程度。点の数を減らす描き方（線の segment を 1 つの shape にする）は L3 の候補。

## 見つけたこと・制限

- 試験の誤りを直した: 最初は「消す」の key の座標が key の間の隙間だった。認識の数の期待値も、手順の前後の差で見るようにした。
- 手書きの認識は stub（ユーザーの判断どおり）。本物の engine は `zwl_hand_recognize` の中に差す（Future）。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は p021（縁に組み込んだ見た目、main の順）、その後 p020。
