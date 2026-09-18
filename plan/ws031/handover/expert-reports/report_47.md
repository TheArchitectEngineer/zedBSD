# WS031 第47報 — 実 preemption scheduler 試験 + PCODE 追加区間 2 試験を実行結果まで完了。sub-tick sleep の dynticks 化は制約を確認し次段へ。GPU-free 検証(ktest 159/0)

ご指示の残項目のうち、**予定済みだった実 preemption 試験と PCODE 追加区間 2 試験を実行結果まで完了**しました。build 0 error/warning。**実 GPU は渡さない構成で検証**。

実機(`run-parity-nogpu.sh`、chaos、image 3098b4ff):`CAS-SELFTEST PASS` / **ktest 159/0**(156→159)/ **probe=NOT_RUN** / selftest=PASS。

---

## 1. 実 preemption scheduler 試験(同一 CPU A/B、実 IRQ→切替延期→最外側 enable)

- `sched_set_cpu()` で thread A/B を**同一 CPU(1)へ pin**。B は wait queue で sleep。A:`kern_preempt_disable()`×2(preempt_count=2)→ **`kern_diag_oneshot_arm(require_cpu=1)`** で次の**実 periodic timer IRQ** から callback を発火 → callback が `waitq_wake_one(B)`(B を runnable + need_resched)。A は fired を**非 blocking**に待ちます(禁止区間で B の通知を blocking wait しません=有限期限の spin)。
- 観測(trace):**fired 後 B は未実行(切替延期)/ 内側 enable(2→1)でも未実行 / 最外側 enable(1→0)で `sched_yield()` → B 実行**。実 IRQ・切替要求・延期・最外側処理・実切替の順を記録。boot 成功だけを合格にせず、**実 IRQ から切替要求が発生した実行のみ**合格としています。最外側 enable の内部で切替が起きる実装は正常として扱っています。
- 補助:`sched_test_preempt_count()`(test 専用)を追加。count を直接ゼロ書きで回復していません。

## 2. PCODE 追加区間 2 試験(scripted time、通常/追加を別証拠)

fake の応答/時間源を **preempt 状態で分岐**(`sched_test_preempt_count() > 0` = 追加区間)させ、決定的にしました。

| 入力 | 合格 |
|---|---|
| **(a)** 通常区間未承認・追加区間で承認 | reply を preempt-off 区間でのみ READY → 通常区間 timeout → 追加区間(`kern_preempt_disable`)で承認 → **ret=0、preempt 復元(count 0)、mutex 解放(trylock 成功)** |
| **(b)** 追加区間で counter 読出失敗 | 追加区間の `kern_rtc_read_counter` を失敗 → parity_pcode_poll が **-EIO(通常 timeout -ETIMEDOUT=-42 でない)**、**preempt 復元・mutex 解放** |

udelay/atomic poll と追加 50ms 区間(preemption 禁止・sleep なし・IRQ を長時間禁止しない)は変更していません。実 scheduler 試験と scripted-time の PCODE 分岐試験は別の証拠として記録しています。

## 3. sub-tick sleep の期限 timer 化(dynticks)— 制約の確認と次段の計画

ご指摘のとおり、第46報の実装では **PCODE の usleep_range(10,20) は busy-wait 分岐**に入ります。これを期限割込みで待つ経路にするには**高分解能 one-shot clockevent** が必要です。今回、実装前に基盤を確認しました。

- **当機には HPET が無く、per-CPU の LAPIC timer が periodic(KERN_CLOCK_HZ=100 / 10ms)で scheduler tick と共用**されています(別系統の空き timer はありません)。
- したがって sub-tick(例:2-3ms を正しく待つ)を実現するには、ご指示どおり **LAPIC を one-shot 化**し、**`kernel_ticks` を IRQ 計数から単調時刻由来へ**変え、割込みごとに**次イベント = min(次 10ms tick 境界, 最早 sleep 期限)** で再武装する **dynticks 変換**が必要です(次イベント調停・過去期限の満了処理・tick を IRQ 一回で数えない・KERN_CLOCK_HZ を上げない・i915 から LAPIC を直接再設定しない、のご条件を満たす形)。
- これは boot-critical な scheduler timer path の変更です。ご承認いただいた HAL 変更範囲として、**次増分で段階的に boot 健全性を確認しながら**実施します(対象:`lapic.c` の one-shot モード + `amd64_lapic_timer_arm`、`clock.c` の `kernel_timer_handler` を単調 tick 化、sleep 期限を counter 単位化して LAPIC 再武装と waitq へ接続)。万一 boot を損なう場合は **本報の 159/0 hybrid へ revert** し、原因を添えて再提示します。
- あわせて、**sleep 試験の合格条件**もご指示どおり是正します(最小時間を要求値の切上げ換算 counter 差と直接比較、max_us 以内 return を要求しない、待機理由=thread/CPU/期限・timer 登録/満了/起床・取消/終了同期を trace 記録、sub-tick 2-3ms と 10-20µs で期限 timer 経路へ入る確認)。

## 4. 残(実 GPU 受入前)

1. **sub-tick sleep の dynticks 化**(上記、唯一の未完点)+ sleep 試験合格条件の是正。
2. **fuse 遅延(100µs)試験**:同じ fake 入力の variant で、即時応答でなく slow 側(fast=2µs→slow=1ms)に入って成立を観測、不成立は正本の警告処理(時間源異常と区別)。
3. **probe.c の UNIMPL を実 `intel_power_domains_init_hw(false)` 呼出へ置換**(P0→P1→P2→P3 前半 → 同じ device で init_hw → 適合層 fault なし確認 → INIT 参照保持のまま intel_dmc_init 入口、DMC 未実装で正確停止、先に cleanup しない)。本番入口で試験 override(MMIO/PCODE/時間/VGA I/O、parity_wait_test_reset_fault)が残っていないことを確認。
4. **D3 四ケース最終再実行** → 上記が揃ったら**参照条件(4 GiB / 4 vCPU / 39-bit、parity 単独、attach 先行)で実 GPU を一回**。到達=電源 HW 初期化完了 → INIT 参照保持 → DMC 入口(P3 全体/描画ではない)。到達時 `last_completed_op=intel_power_domains_init_hw` / `blocked_or_failed_op=intel_dmc_init` / init_wakeref 保持、診断終了後は cleanup/PM 参照処理/published=0 を分けて記録。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE+三層 retry、combo PHY、CDCLK、D3 親、fuse/DC_off/VGA、実 sleep(hybrid)、実 preemption、PCODE 追加区間。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索・baremetal へは戻りません。

次報:sub-tick sleep の dynticks 実装(lapic.c / clock.c / sleep 期限)と是正した sleep 試験結果、fuse 遅延試験、probe.c の実接続。実 GPU を使った場合は到達点と cleanup。`last_completed_op` は attach 未実行のため該当なし(GPU-free)。
