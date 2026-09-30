<!-- awesome-plan project=zedbsd record=ws102-p003 -->

# ws102-p003: flick の表・方向の判定・key と花びらの描画

Status: cleared（2026-09-30、QEMU の Venus と host。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`）

## 範囲と受け入れ

design §2.4 と §4 の p003 を扱う。

- `keyboard-layout.c` に、flick の 3 つの face の表、方向の判定、濁点・小書きの巡り、大小の切り替えを置く。
- panel の key を描き、押した時の花びらを出す。
- host の試験を足す。

受け入れ（L1 の (b)）: かな 46 字と「ー」・英字 26・数字 10・ASCII の記号 32・空白の全てを、表の上の flick で出せる（host）。

文字を app へ送るのは p004 の範囲。p003 では、離した key の文字を log に出すところまでとする。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/wayland/keyboard.h`（新規） | 配列の型と API（`zwl_flick_key`・`_face_name`・`_face_next`・`_direction`・`_text`・`_voice`・`_case`・`_direction_name`）。Wayland と描画に依らない |
| `userland/desktop/wayland/keyboard-layout.c`（新規） | かな・英字・数字と記号の 3 つの face（4 列 × 4 行。右の列は Del・空白・改行・face の切り替え）。<br>方向の判定: `max(16, key × 0.3)` px 未満は中心。それ以上は近い軸の向きで、同じ長さなら横。<br>濁点の巡り: か→が→か、は→ば→ぱ→は、つ→っ→づ→つ、あ→ぁ など。<br>大小の切り替え: a↔A |
| `keyboard.c` | flick の panel に key を描く（文字の key は白、操作の key は灰、押した key は青）。label は 1 文字なら 24 px、それより長ければ 20 px。<br>key を押すと、花びらを左・上・右・下に出す（不透明な白に影、向いている方は青）。<br>離すと、face の key は次の face へ替わる。他の key は `ZWL OSK key face= row= column= dir= action= text=` を log に出す。<br>帯の題は今の face（かな・ABC・123）。閉じる key の円を濃くした（p002 の注） |
| `Makefile` | `keyboard-layout.c` |

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … build/ws102-amd64/bin/wayland` | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c keyboard-layout.c keyboard.h` | 0 件（試験の `host-keyboard.c` には 7 件。試験の code なので p014 で扱う） |
| host | `sh plan/ws102/tests/host-keyboard.sh` | PASS。確かめた内容: 英字 26・数字 10・記号 32・空白が英字か数字の face にある。かな 46 字と「ー」がかなの face にある。どの face でも同じ文字が 2 回出ない。方向の判定 9 件、key の文字 2 件、濁点 6 件、大小 3 件 |
| guest（QEMU の Venus、pen image の複写。fallback の字体 `/usr/share/fonts/keiland-fallback.ttf` を試験で入れる） | `plan/ws102/tests/osk-guest.sh build/ws102-shots/p003 install start pointer flick edges touch` | PASS（p002 の手順も含む） |

手順 flick で確かめたこと（log と画面）:

- あ を tap して `text=あ`、か を左へ flick して `text=き` になった。
- な を上へ押したまま（`petals.png`: に・ぬ・ね・の の花びらで、ぬ が青い）離して `text=ぬ` になった。
- face の key で英字へ替わり（`alpha.png`）、abc の tap で `a`、上への flick で `c` になった。
- 数字へ替わり、1 の tap で `1`、2 を下へ flick して `>` になり、かなへ戻った。
- 指（touchinject）で あ を右へ flick して `text=え` になった。
- かなの判定は、guest の shell を通すと UTF-8 の grep が当てにならないので、log を host へ持ってきて比べる（`expect_text`）。

## 見つけたこと・制限

- 注入の試験に使う pen image（`build/main-pen`、9/29）には fallback の字体が無く、かなが豆腐になった。試験の install で字体を入れる。今の main の image には package で入っている（`wayland/Makefile`）。
- 白い glass の上の白い文字の key は、境目が淡い（`alpha.png`）。L3 の見た目の磨き込みで、縁か影を足すかを決める。
- 濁点と大小の key は、log に `action=5`・`action=6` を出すだけ（直前の文字を置き換える処理は、送出を扱う p004）。
- 実機・Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は p004（文字の送出）。
