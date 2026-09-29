<!-- awesome-plan project=zedbsd record=ws075p008 -->

# ws075-p008: 性能 1: 完了待ちを割込みへ（ws031-p044）

Phase ID: `ws075-p008`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-29）
Phase disposition: normal
承認: 2026-09-29 main の依頼（WS075 の i915 の subagent、項目 1「ws075-p008（性能）」。大きければ計画の段階で分けて 1 つずつ）。

## 分割（2026-09-29 計画）

元の p008（性能: 完了待ちの割込み・非同期の実行器・present mode と vsync）は 3 つの独立した変更で、非同期の実行器だけで
WS031 の見積もりが 5〜8 日ある。依頼の「大きければ分ける」に従い、次の 3 Phase に分けた（範囲の正本は WS031 の
[phase018](../../ws031/phase018/phase.md) の 1・2・5）。

| Phase | 範囲 | 依存 |
| --- | --- | --- |
| ws075-p008（この Phase） | 完了待ちを CSB の busy-poll から GT の割込みと waitq へ（ws031-p044） | p002 |
| [ws075-p018](../phase018/phase.md) | 非同期の実行器（ws031-p045）: submit を待たずに返し、fence・semaphore は GPU の完了で signal | p008 |
| [ws075-p019](../phase019/phase.md) | present mode と vsync（ws031-p027）: FIFO・MAILBOX・IMMEDIATE を client が選ぶ | p008 |

## 範囲

request worker（`worker.c`）が engine に出した request の終わりを待つ所は、CSB を処理して 50 µs の busy-wait
（`drv_i915_udelay`）を最長 10 秒くり返す。GPU が 1 回の submit に約 6 ms（RP0）かかる間、worker の CPU は回り続け、
同じ CPU の他の thread（compositor・client）は quantum が尽きるまで待たされる。

この Phase: worker は request の終わりを、GT の engine の割込み（final breadcrumb の MI_USER_INTERRUPT と、CSB に entry を
書く context switch の割込み）で起こされるまで眠って待つ。

非範囲: 非同期の実行器（p018）、present の待ち（p019）、CSB の処理を割込みの側へ移すこと（tasklet）、RPS の割込み（F-054）。

## 設計

- `irq.h` の `struct i915_irq_dev` に engine の事象の wait queue とその lock（`engine_lock`・`engine_waitq`）と、起こした回数を足す。
  `drv_i915_irq_install()` が handler を付ける前に初期化する。
- 割込みの handler（`i915_gt_engine_irq()`）: user interrupt か context switch の bit があれば `engine_lock` の下で
  `waitq_wake_all()`。display の vblank の handler と同じ形（IRQ の文脈で DEVICE の rank の spinlock と waitq）。
- 公開の 2 関数（`irq.c`）: `drv_i915_irq_engine_sequence()`（今の sequence）、`drv_i915_irq_engine_wait()`（観た sequence から
  進むまで、または期限まで眠る）。sequence を CSB の処理の前に読むので、処理と眠りの間の割込みは失われない（sleep が EAGAIN）。
- `worker.c` の `i915_worker_wait()`: 期限（10 秒）を tick で持ち、CSB の処理 → 完了の判定 → 割込みを待つ、をくり返す。
  待ちは最長 1 tick（1 ms）で、割込みが失われても 1 ms ごとに CSB を見る（安全網）。割込みの handler が付いていない
  （`gt.irq_installed == 0`）ときは今までの 50 µs の busy-wait のまま。
- 起きた理由（割込み・期限）を worker に数え、停止の log の行に出す（解析の補助。判定には使わない）。

## 検証

- build（warning 0）: demo の passthrough の image（`plan/ws075/demo/build-demo-image.sh BUILD passthrough ZEDBSD_GRAPHICAL_BOOT=n
  "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`）。
- 規約: `plan/tools/style-check.py` を変えた file に。
- QEMU（Venus）: `plan/tools/boot-test.sh`（GPU の無い login prompt）。i915 は QEMU に無いので Venus の回帰は i915 の変更の影響を受けない
  （起動の確認のみ）。
- 実機（5330 の passthrough、Phase の終わりに 1 回、`flock /tmp/i915-hw.lock`）: 変更の前後の image で `h4-ctl.py latency A 10`
  （入力から flip までの時間と入力なしの flip の数）。desktop が描かれ、flip が進むこと（`shot`）。
- 受け入れ: worker の待ちが割込みで起こされて request が終わり（desktop の操作で flip が進む）、入力から flip までの時間が
  変更の前より悪くならない。

## 記録

（実施中）
