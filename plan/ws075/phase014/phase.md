<!-- awesome-plan project=zedbsd record=ws075p014 -->

# ws075-p014: BUG-091 の修正（i915 の spin lock を割込み許可のまま持つ所）

Phase ID: `ws075-p014`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28。原因を特定して修正、実機の passthrough で Notes・PDF Viewer の起動 48 回で fault 0、host の contract 試験と
GPU の無い QEMU の boot test も PASS）
Phase disposition: normal
承認: 2026-09-28 main の依頼（デモ 2026-10-17 の前に直す BUG-091・BUG-092、i915 の実機の占有）。

## 範囲

- [BUG-091](../../bugs/BUG-091.md): i915 の timer thread が `timers->lock` を CPU0 で取り CPU1 で離して `spin_unlock` の `ud2` で止まる。
  kernel の `spin_lock` が preemption・migration を止めるか、`sched_sleep_locked` が渡される lock に何を求めるか、i915 がどう使うかを
  調べ、誤っている側を直す。HAL の API は変えない。
- [BUG-085](../../bugs/BUG-085.md) との関係の見立て。

## 調べたこと

- `spin_lock()`（`src/kern/lock.c`）は割込みも preemption も止めない。持ち主の CPU を記録し、別の CPU の解放と同じ CPU の再取得を
  trap にする。`sched_clock_cpu()` は quantum の尽きた kernel thread も `sched_yield()` で preempt し、暇な CPU は run queue の thread を
  盗む。preempt の数（`kern_preempt_disable()`）は spin lock と連動しない。
- `sched_sleep_locked()` の契約（`include/kern/sched.h`）は「IRQ-safe の condition lock」。kernel の `waitq_sleep()`・
  `sched_sleep_locked*()` の呼び手 94 箇所を機械的に照合: condition lock を同じ関数の中で `spin_lock_irqsave()` で取っていないのは
  i915 の `workqueue.c`（timer queue）・`sync.c`・`ktest-sync.c` と kernel の `kern_usleep_range()`（`src/kern/clock.c`）だけ
  （他は `*_locked` の helper で呼び手が irqsave）。
- 割込み許可の thread の文脈で `spin_lock()` を取る所（sleep しないもの）: i915 の `device.c` の start registry（5 関数）、
  `request-queue.c` の `drv_i915_request_complete_list()` の `device->irq_lock`（retire の度）。`display/hotplug.c` の 2 箇所と
  `text-display.c` は割込み禁止の中なので問題ない。
- 結論: kernel の sleep の経路は契約どおりで、**i915 が契約を破っていた**（と `kern_usleep_range()`）。

## 変更

| 所 | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/workqueue.c` | timer queue の lock を全て `spin_lock_irqsave()`/`spin_unlock_irqrestore()`（destroy・delayed_queue・cancel・cancel_sync・pending・timer thread）。file の注記を直した |
| `src/drivers/gpu/i915/device.c` | start registry の lock を irqsave に（5 関数）。struct の注記 |
| `src/drivers/gpu/i915/request-queue.c` | retire の slot の返却の `device->irq_lock` を irqsave に |
| `src/drivers/gpu/i915/sync.c`・`tests/execution/ktest-sync.c` | 1 tick の sleep の局所の lock を irqsave に |
| `src/kern/clock.c` | `kern_usleep_range()` の局所の lock を irqsave に |
| `src/kern/lock.c`・`src/kern/waitq.c` | `spin_lock()` と `waitq_sleep()` の注記に規則（thread は割込み禁止の所でだけ `spin_lock()`、condition lock は irqsave）を書いた。実装は不変 |

HAL（`include/hal/hal.h`・`src/hal/`）は変えていない。同じ build には ws035-p113（BUG-092）の変更も入っている。

## 確認

| 確認 | 結果 |
| --- | --- |
| build（`plan/ws075/demo/build-demo-image.sh build/b091 passthrough`、worktree） | 成功。warning は外部の Noct の既存の 1 件だけ（kernel・driver・sessiond は 0）。共有の `build/llvm` は触っていない（BUG-089 の回避: `build/llvm`・`llvm-source` 等を main への symlink、`toolchain/llvm/distfiles` を symlink、patch の mtime を main に合わせた。install の stamp は 2026-09-27 のまま） |
| host: `src/drivers/gpu/i915/tests/contracts/run.sh sync` | 42 checks 0 failures（通常・ASan/UBSan） |
| **実機**（5330、VFIO passthrough の QEMU guest、`hdmi-h4-hw.sh`、2026-09-28 15:35〜16:10） | greeter → login → App Home から Notes を 24 回（毎回 mouse の drag で 4 本の stroke、Ctrl+W で閉じる）、PDF Viewer を 24 回（Ctrl+W）、合計 48 回の起動。各回の画面を scanout の buffer で確認（23 回の loop の全て Notes の page・PDF Viewer の窓が出た）。kernel の log の `amd64 fault`・`fatal`・`unhandled` は 0。lease 1・2 は `ended PASS ... first anomaly: none`、lease 3（Log Out の後の greeter）も正常。証拠: worktree の `build/b091-run1/`、`build/ws075-shots/bug091-*.png` |
| QEMU: `plan/tools/boot-test.sh build/b091-boottest.img`（GPU の無い q35） | PASS（`build/b091-boottest/login.png`） |
| 規約: `plan/tools/style-check.py` の変えた行 | 指摘は critical section の終わりの unlock の行（既存の同じ形の行と同じ、§5 の lock と unlock の段落）だけ |

H4 の fault の再現率は元々未測定（1 回）。48 回の無事は原因の除去（コードの読みで確定）に加えた、再現しないことの証拠であり、証明ではない。

## H4 の greeter の画像が黒かった件（main の追加の依頼）

`build/ws075-shots/hdmi-h4-greeter-hdmi-1920x1280.png`（H4）は resident display の **表示していない方の buffer（A）** だった。
greeter の lease は picture up で A（黒）を出し、frame 11〜12 の flip 1 で B（greeter の絵）へ移る。greeter の絵は変化が少なく、
次の flip は数十秒後（時計）なので、その間 A は黒のまま。今回の実機の Log Out の後の greeter（lease 3）で、lease の開始から 1.4 秒ごとに
8 回撮った: t1〜t7 は A が黒・B が greeter（log の最後の flip は `0xbc0c0000 -> 0xbcb80000` = B）、t8 の直後の flip 2 の後は両方 greeter。
**greeter は HDMI に描いている。H4 の撮影は間違った buffer を選んだ**（早すぎたのではない）。画像: worktree の
`build/ws075-shots/h4q-greeter-early-idle-buffer-A-black.png`・`h4q-greeter-early-scanout-buffer-B.png`。撮影の道具は live の buffer を
kernel の log の最後の flip から選ぶ必要がある（今回は scratchpad の helper で行った。`h4-ctl.py` への取り込みは未実施）。

## BUG-085 との関係

BUG-091 の形の誤りは起きれば必ず trap（fatal の報告）になり、他の CPU の待ちは持ち主が走れば終わる。BUG-085 の停止には fault の報告が
無いので、同じ原因の可能性は低い（ticket に記録）。retire の `irq_lock` も同じ形だったので、修正後の kernel での BUG-085 の再試行は価値がある。

## 未実施・残り

- bare metal での確認（passthrough の QEMU guest だけ）。pen（タブレットのペン）での描画は未実施（mouse の drag）。
- `h4-ctl.py shot` の live buffer の選択（上）。
- Venus（virtio-gpu）の QEMU の boot test は未実施（変えたのは i915・`kern_usleep_range`・注記で、GPU の無い boot test で kernel は確認）。
