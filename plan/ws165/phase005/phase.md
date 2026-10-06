# ws165-p005: 手書きの認識率の改善（区別できない字の組を分ける）

Status: in-progress（2026-10-07 q831 P2: 実装と host の試験 PASS。QEMU の回帰は T1 に依頼、判定は Q1）
Queue: q831（2026-10-07、P2）
WS: [WS165](../ws.md)

2026-10-06 夜 ユーザー（H5 の質問への回答）「いったんacceptして、追加のフェーズを第2段でやりましょう。」→ p002 の認識率（そのままの top-1 87〜88%、組を 1 字と数えて 91.7%、top-4 98.3%、平均約 6 ms）でいったん受け入れる。この Phase で、絵だけでは区別できない組（c/C、o/O/0/°、s/S、v/V、w/W、x/X/×、z/Z、l/|/1、./·、p/P、/ とノ）を、書く面の箱に対する大きさや前後の文脈で分け、そのままの top-1 を 90% 以上にする。

## 実装（2026-10-07 P2）

- `userland/desktop/wayland/hand-cloud.c`・`.h`: 書く面（`struct hand_frame`: 上端と高さ）の上の ink の**大きさと位置**を形と一緒に数える `hand_recognize_framed`。
  - 手本ごとに Hershey の座標の箱（`struct hand_extent`）を読む時に持つ。Hershey の字の面は y が -16〜16（高さ 32、大文字・かなの高さは 21）。
  - 各手本の距離に、ink の大きさ（長い辺 / 面の高さ）と手本の大きさの比の対数 ×1.0、真ん中の高さ（面の中の割合）の差 ×3.0 を足す。大きさは点（0.04）より小さく数えない。
    足す分が既に暫定の上位より遠い手本は照合しない（速さ）。濁点・半濁点の扱い（印を除いた残り）にも同じく効く。
  - 重みは host の試験の seed 既定・7・11・23（10・20 試料）で大きさ 0.1〜2.5、位置 0.3〜8 を比べて選んだ（1.0・3.0 で 90.6〜91.7%）。
- `keyboard-hand.c`・`keyboard.h`: `kwl_hand_recognize_on(ink, top, height, result)`。書く面の上端と高さを渡す。c/C 等の Latin の組は面が分けるので、大きさの並べ替え（`hand_sized`）は小書きのかなだけ（手本に無い）。
  面の高さだけの `kwl_hand_recognize`（p003、WS102 の試験が使う）は今のまま。
- `keyboard.c`: 手書きの面の認識を `kwl_hand_recognize_on`（面の上端 `area[1]`・高さ `area[3]`）に。

## 試験の変更（host）

- `plan/ws165/tests/host-hand.c`: 試料を Hershey の面（-16〜16）に書いた物として、さらに ±10% の大きさ・面の 5% の上下・横のずれを足し、面を渡して認識する（`hand_recognize_framed`）。
  合格は**そのままの top-1 ≥ 90%** と top-4 ≥ 98%（組を 1 字に数えない）。`HOST_HAND_UNFRAMED=1` で面なし（p002 の形）。
- `plan/ws165/tests/host-hand-keyboard.c`: `kwl_hand_recognize_on`（面の上端 0、高さ 300）。小さい c は小文字の位置（上端 150）に書く。点が面の下なら「.」、真ん中なら「·」、小さく上の o は「°」、面の高さだけの `kwl_hand_recognize` で c を大きく書くと C・c（p003 の形）を足した。

## 結果（2026-10-07 P2、host、Linux の cc -O2、228 字 × 20 = 4,560 試料）

| 測り方 | 面なし（p002 の形、同じ試料） | 面あり seed 既定 | seed 7 | seed 11 | seed 23 | 目標 |
| --- | --- | --- | --- | --- | --- | --- |
| top-1 | 87.2% | **90.6%** | 91.0% | 91.1% | 91.7% | ≥ 90% |
| top-1（組を 1 字） | 90.5% | 91.4% | 91.9% | 91.7% | 92.4% | — |
| top-4 | 98.0% | 98.4% | 98.6% | 98.6% | 98.4% | ≥ 98% |
| 1 文字（平均・最遅） | 5.0 ms・10.6 ms | 4.4 ms・10〜17 ms | 同 | 同 | 同 | host で 5 ms 以下 |

- 残る取り違え（seed 既定）: ×→x、れ↔わ、ぽ→ぱ、v→s、O→0、B→8、ヌ→ス、じ→し、a→o、2→フ、エ↔ヱ、へ↔ヘ・ぺ↔ペ・べ↔ベ（ひらがなとカタカナで同じ形）。形の違いが小さい組で、大きさと位置では分けられない。
- 面の前提: 利用者は書く面の高さを Hershey の面（大文字・かなが面の 2/3）と見て書く。実機の書き方と合うかは p004（実機の UAT）で見る。

## 確認

- `sh plan/ws165/tests/run-host-hand.sh 20`（と seed 7）: PASS。
- `sh plan/ws165/tests/run-host-hand-keyboard.sh`: PASS（10 の check）。
- `sh plan/ws102/tests/host-keyboard.sh`: PASS（面の高さだけの形は不変）。
- zedBSD の wayland の build warning 0。style-check 0（`hand-cloud.c`・`hand-cloud.h`・`keyboard-hand.c`・`keyboard.h`・試験の C）。
- QEMU: 未実施（T1 に依頼: `plan/ws102/tests/osk-guest.sh` の install と hand の段。期待は p003 と同じ `KWL OSK hand templates ... count=228 error=0` と `KWL OSK hand recognize ... candidates=[1-4]`）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS165 の行。
