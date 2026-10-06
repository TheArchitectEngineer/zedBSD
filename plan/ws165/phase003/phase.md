<!-- awesome-plan project=zedbsd record=ws165-p003 -->

# ws165-p003: compositor の手書きの認識（`kwl_hand_recognize` を本物に）

Phase ID: `ws165-p003`
Parent: [WS165](../ws.md)
Status: in-progress（2026-10-06 P2: 実装と host 試験。T1 待ち）
Phase disposition: normal
Queue: Q1 の P2 の列（2026-10-06）

## 範囲

[p002](../phase002/phase.md) の照合を、WS102 の手書きの面（`keyboard-hand.c` の stub）に入れる。区別できない組の改善は必須にしない（[p005](../phase005/phase.md)、第 2 段、2026-10-06 Q1）。

## 実装（2026-10-06 P2）

- `userland/desktop/wayland/keyboard-hand.c`:
  - `kwl_hand_load(path)`: templates を最初の認識の時に `/usr/share/keiland/hand/hershey.txt`（`KEILAND_DATADIR`）から読む。log は `KWL OSK hand templates path= count= error=`。
  - `kwl_hand_recognize(ink, area, result)`: `area` は書く面の高さ。ink を `hand_recognize_strokes` に渡し、候補を最大 4 つ UTF-8 で返す。templates が無い時は候補 0 と note「No handwriting data」。stub の「あいう」と「認識はまだ」は除いた。
  - 大きさの並べ替え（簡単な形だけ。本格的な改善は p005）: 先頭の候補が大小の組（c/C 等、小書きの仮名 ゃ/や 等）なら、ink が面の高さの 40% 未満の時は小さい方、それ以外は大きい方を先にし、もう一方をすぐ後に置く。Hershey に無い小書きの仮名は、こうして候補に出る。
- `keyboard.c`: 書く面の高さを渡す。`keyboard.h`: 関数の形。
- build: wayland の `Makefile`・`Makefile.linux`・`Makefile.freebsd` に `hand-cloud.c` を足した。wayland の package が `fonts/hand-hershey` を要る（`noto-color-emoji` と同じ）ので、compositor の入る image には templates が入る。
- WS102 の試験を変更に合わせた（どちらも WS102 の未完了の Phase と回帰で使う）。
  - `plan/ws102/tests/host-keyboard.c`・`.sh`: stub の確かめを、templates が無い時に note が出ることの確かめに替えた。
  - `plan/ws102/tests/osk-guest.sh`: install で templates を guest に置く。hand の段で `count=228` と候補（1〜4、note 無し）を見る。

## 確認（host）

| 確認 | 結果 |
| --- | --- |
| `plan/ws165/tests/run-host-hand-keyboard.sh`（あ、c を大きく書くと C・c、小さく書くと c、や を小さく書くと ゃ・や、が） | PASS |
| `plan/ws102/tests/host-keyboard.sh` | PASS |
| `plan/ws165/tests/run-host-hand.sh` | p002 のとおり |
| build（zedBSD の wayland、Linux の `keiland-linux.mk all`） | warning 0 |
| style-check（`keyboard-hand.c`・`hand-cloud.c`・試験） | 0 |

未実施:
- QEMU（T1）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk hand-hershey` の後、`plan/ws102/tests/osk-guest.sh install` と `hand` の段。期待は `KWL OSK hand templates ... count=228 error=0` と、`KWL OSK hand recognize strokes=3 ... candidates=[1-4] first=... note=`。
- FreeBSD の build。
- 実機（p004）。
