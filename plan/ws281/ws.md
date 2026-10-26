<!-- awesome-plan project=zedbsd record=ws281 -->

# WS281 libc の malloc を速くする

- Master: [plan/master.md](../master.md)（2026-10-22 作成、着手は後日）。Primary Milestone: MG002。Related: MG006（desktop の応答）。
- Status: planning・保留（後日）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-22 ユーザー「mallocは速くないと思います。これはWSを作って、後日取り組みましょう。」）。旧: planning
- 出典: 2026-10-22 ユーザー「mallocは速くないと思います。これはWSを作って、後日取り組みましょう。」

## 背景

- `src/libc/heap.c` は空きの block の 1 本の list を先頭から順に探す first fit で、heap 全体を 1 つの lock で守る（ws270-p007 の P2 の読み、2026-10-22）。
- 長く動く process（Keiland など）では空きの list が長くなり、thread が多いと lock の取り合いが起きる。VF2 の desktop では libvulkan の wire の 1 往復ごとに malloc・free が 3 組ほどあり、往復の中の user の時間（70〜200 us）の原因の候補になっている（未確認）。

## 目的と完了の条件（案）

- malloc・free・realloc・calloc の速さを、大きさの分布と thread の数を変えた試験で測り、今の実装より明らかに速くする（目標の数は着手の時に決める）。
- 長く動いた後の断片化でも遅くならない（大きさの class ごとの bin など）。
- 正しさ: 既存の libc の試験と、thread を使う試験が通る。全部の arch で build の warning 0。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 計測（今の heap の速さと断片化、desktop の process での malloc の時間）と設計 | — | planning（着手はユーザーの指示まで） |

## 現在の状態・再開点

- 2026-10-22: 作成だけ。ws270-p007 では malloc を直さない（ユーザーの判断）。
