<!-- awesome-plan project=zedbsd record=ws075p008 -->

# ws075-p008: 性能 1: 完了待ちを割込みへ（ws031-p044）

Phase ID: `ws075-p008`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-29。実機の passthrough で request の終わり 150 回中 149 回が割込みで起こされた待ちの直後に見つかり、入力から flip の時間・flip の率は変更の前と同じ。vkx 9/9）
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

## 記録（2026-09-29）

### 変えた file

- `src/drivers/gpu/i915/irq.h`: `struct i915_irq_dev` に `engine_lock`・`engine_waitq`・`engine_wait_ready`・`engine_wakeups`。
  `drv_i915_irq_engine_sequence()`・`drv_i915_irq_engine_wait()` の宣言。`<kern/lock.h>`・`<kern/waitq.h>` を読む。
- `src/drivers/gpu/i915/irq.c`: install が handler を付ける前に queue を 1 回だけ用意する。`i915_gt_engine_irq()` が user interrupt と
  context switch で `i915_engine_wake()`（lock の下で数えて `waitq_wake_all()`）。上の 2 関数。
- `src/drivers/gpu/i915/worker.c`: `i915_worker_wait()` を上の設計のとおりに（期限は tick、sequence は CSB の処理の前に読む、
  1 tick の安全網、handler が無ければ 50 µs の busy-wait）。起きた理由の数（`wait_interrupts`・`wait_timeouts`）と停止の log の行。
  HAL・UAPI は変えていない。
- `plan/ws075/tests/hdmi/h4-ctl.py`: `rate PIPE SECONDS`（pointer を 8 ms ごとに 40 px 往復させながら PLANE_SURFLIVE の変化を数え、
  入力の続く間の flip の率を出す）。以後の性能の Phase の物差し。

### 検証

| 確認 | 結果 |
| --- | --- |
| build（`plan/ws075/demo/build-demo-image.sh build/ws075-p008/pt passthrough ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`） | 成功、この tree の source の warning 0（`-Werror`）。log の warning は外部 package（openssh の deprecated）と perl の locale だけ |
| 規約（`plan/tools/style-check.py`） | 変更の前後の差は critical section の本体と unlock の行の paragraph-comment（既存の worker.c と同じ形、規約 §5 の critical section の形）と log の文言の変化だけ |
| QEMU の boot test（GPU なし、`plan/tools/boot-test.sh build/ws075-p008/irq.img`） | PASS（`build/ws075-p008/boot-test/login.png`） |
| Venus の回帰 | 未実施（Venus の driver・共通の GPU core は変えていない。i915 の file だけ） |
| host の i915 の試験 | 該当なし（worker・irq は host の fixture に無い。`run-vk-host-tests.sh` の範囲は render/ と compiler/） |

実機（5330 の VFIO passthrough の QEMU guest、4 vCPU、KVM。`plan/ws075/tests/hdmi-h4-hw.sh`、`flock /tmp/i915-hw.lock` の下）。
image は変更の前 `build/ws075-p008/base.img`（main 249e5ab1 の tree）と後 `build/ws075-p008/irq.img`、同じ引数の demo の passthrough の image。
desktop（kei の自動の login の session）の起動の約 100 秒後から、PLANE_SURFLIVE（pipe A）を QMP の xp で読んで測った:

| 物差し | 変更の前 | 変更の後 |
| --- | --- | --- |
| `latency A 10`（pointer の 40 px の移動から次の flip まで） | 10/10、中央値 49.2 ms（48.7〜99.2） | 10/10、中央値 49.3 ms（48.7〜115.6） |
| 入力なしの 3 秒の flip | 0 | 0 |
| `rate A 10`（入力の続く 10 秒の flip の率） | 19.6/s・19.7/s | 19.7/s・19.4/s・19.5/s |
| 画面 | desktop（`build/ws075-shots/ws075-p008-base-desktop.png`） | desktop（`build/ws075-shots/ws075-p008-irq-desktop.png`） |

割込みで起きていることの確認（gdbstub。HMP の `gdbserver` で動いている guest に後から付け、DWARF 無しの vmunix の disassembly の offset で読んだ）:

- `i915_worker_wait` の入口で worker を取り、数を読んだ: 1652 request（executed）の時点で engine の割込みの wakeup 2871（request あたり約 1.7:
  final breadcrumb の user interrupt と context switch）、待ちの終わり方は割込み 2731・1 tick の期限 13861。
- request の終わりを見つけた所（`i915_worker_wait+485`）に hardware breakpoint を置き、直前の眠りの結果（`%ebx`）を 150 回: 0（割込み）149 回、
  ETIMEDOUT 1 回。**request の終わりはほぼ全て割込みで知る**。1 tick の期限は GPU の実行中（約 6〜15 ms）に 1 ms ごとに worker を起こす
  （上の 13861）が、終わりの検出は割込みが先に来る。期限で終わりを見つけたのは 150 回に 1 回（割込みが無かったか遅れた）。
- 実行器の回帰: `plan/ws075/tests/test-hw.sh vkx`（i915 の試験の build、`BUILD=build/ws075-p008/vkx`）: 試験の場面の verdict の行 PASS（9 of 9）。
  WS075 の既存の場面と同じく試験の build の verdict の行で判定した。

### 所見（次の Phase へ）

- passthrough では入力から flip まで約 49 ms で一定、入力が続いても約 20 flip/s。worker の待ち方を変えても変わらないので、frame の周期は
  worker の CPU ではなく、client・compositor の submit（GPU の往復 2 回）と present（copy と flip の待ち）の直列の和で決まっていると見る
  （推測。WS084 の bare metal の perf: submit ごとの GPU 6.3 ms × 2、present 11.3 ms、24.5 present/s）。p018（非同期の実行器）と
  p019（present mode）がこれを縮める対象。
- 1 tick の期限は安全網として残した。割込みの取りこぼしが 150 回に 1 回あるので、期限を長くすると取りこぼしの request が遅れる。

### 未実施

- 素の 5330（USB の image）: 未実施（ユーザーの作業）。確かめる点: desktop が動くこと、操作中の present の率（WS084 の 24.5/s）が下がらないこと。
- CPU の使用量の減りの数値（worker の CPU が GPU の実行中に空く）: 測っていない。
