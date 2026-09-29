<!-- awesome-plan project=zedbsd record=ws099p004 -->

# ws099-p004: C1 の残り（起動から greeter・デスクトップ、Shut Down の替わり目）を撮る試験

Phase ID: `ws099-p004`
Parent: [WS099](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「p004: C1 の残り: 起動から greeter、Shut Down の替わり目を撮る試験」）

## 範囲

C1「起動から greeter、login からデスクトップ、Log Out から greeter、Shut Down の各遷移で、黒い画面と文字の console が 0 枚」のうち、
p001 で試験の無かった起動と Shut Down を撮る。login・Log Out は ws035-p126（p001 で PASS）。compositor は変えない。

## 作ったもの（`plan/ws099/tests/`）

- `c1-watch.py`: guest の 2 つの画面を 1 秒に 2〜4 回撮る。標準の VGA（firmware と kernel の絵: 起動の絵か文字の console）は QMP の
  screendump、Venus（compositor の絵）は VNC（frames.py と同じ）。機械の画面は 1 つで、compositor が絵を出した後はそれを、前は起動の絵を
  映すので、各時点の「見える絵」を「Venus が黒でなければ Venus、そうでなければ VGA」とし、frames.py の分類（black・text・picture）で数える。
- `c1-boot-shutdown.sh IMAGE [OUTDIR]`（guest を自分で起こす）:
  1. 起動: QEMU の起動の直後から、kei の autologin のデスクトップ（`SESSIOND HANDOFF go written=3`）+ 3 秒まで。最初の絵（firmware の
     TianoCore の印）から後に、黒・文字の console の時点が `C1_MAX_BLACK`・`C1_MAX_TEXT`（0）以下。最初の絵の前（firmware の起動直後の黒）は別に数える。
  2. Shut Down: App Home の Log Out で greeter に戻し、greeter の Shut Down（右下、1280x800 で (1200,758)）を押して `C1_SHUTDOWN_S`（40 秒）撮る。
     機械が落ちること（20 秒後に SSH が応じない）と、黒・文字の console が 0（最後に続く黒は「終わりの黒」として別に数える）。
- `criteria.sh` の C1: p126 に `--no-black` を付けた（p126 は黒を数えるだけで、`--no-black` のときだけ黒で FAIL にする。C1 は黒 0 が基準）。
  続けて `c1-boot-shutdown.sh`。

## 結果（QEMU の Venus、`build/ws099-criteria.img`）

| 替わり目 | 撮った時点 | 黒 | 文字の console | 内容 |
| --- | --- | --- | --- | --- |
| 起動 | 43（約 14 秒） | 0（最初の絵の後）。firmware の前の黒 5 | 0 | TianoCore（firmware）→ Kei の起動の絵と spinner（1920x1080 の GOP）→ デスクトップ（Venus）。黒も文字も挟まない |
| Shut Down | 84（40 秒） | 0 | 0 | 機械は落ちた（SSH が応じない）。画面は greeter の絵のまま（Venus が最後の絵を保つ）。QEMU は終わらない（この guest の poweroff は機械を止めるが emulator を終わらせない） |

`c1-boot-shutdown.sh` → `C1: PASS`（`C1 RESULT boot_black=0 boot_text=0 shutdown_black=0 shutdown_text=0 shutdown_down=1`）。
画面: `build/ws099-shots/c1-boot/boot/`（seen-005: TianoCore、seen-010: Kei の起動の絵）・`shutdown/`（greeter のまま）、一覧 `boot.txt`・`shutdown.txt`。

途中の失敗（試験の側）: 1 回目は `count_seen` の不要な引数で `set -u` が止めた。2 回目は Shut Down の後に「QEMU が終わるまで」撮ったが、
QEMU は終わらず 90 秒の上限まで撮り、機械が落ちたかを確かめていなかった。SSH の応答で確かめる形にした（debug の実行で、押した後に SSH が切れることを確認）。

## 気づいたこと・制限

- Shut Down の後、画面は greeter の絵のまま止まる（電源を切っている途中だと分かる表示は無い）。黒・文字は出ないので C1 は満たすが、実機で
  電源が切れる前にどう見えるかは未確認（ユーザーの目視）。
- 撮る間隔は 0.25〜0.5 秒なので、それより短い黒の一瞬は見逃しうる。
- 実機（5330）は未実施。

## Resume point

2026-09-30: cleared。`criteria.sh … C1`（`build/ws099-p004/results.txt`）: p126（`--no-black`）PASS、c1-boot-shutdown PASS（黒 0・文字 0、機械は落ちた）。
