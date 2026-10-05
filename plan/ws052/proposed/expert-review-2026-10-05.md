# 専門家のレビュー（2026-10-05、ユーザー経由、原文の要約ではなく要点の写し）

ユーザーが外部の専門家に review-request.md を見せて得た結果。専門家は *.diff・README.md・design.md は見ておらず、提案の API と既存の hal.h に対する設計のレビュー。

全体: 必要な機能は合っているが、API の切り方に S0i3・x86 の実装手順が露出している。特に H1 の hint と H4 の責務の範囲が原因。H1/H2/H4 を hal_system_s0ix() にまとめるのではなく、「per-CPU の suspend-idle の mechanism」と「system-wide の IRQ の policy」の 2 層に整理するのが推奨。

| 案 | 評価 | 専門家の案 |
| --- | --- | --- |
| H1 idle_deep(hint) | 形を変更 | `int hal_cpu_idle_suspend(void)`（current CPU を system suspend-to-idle に適した最深の idle state に入れ、interrupt で戻る。state・hint は HAL が選ぶ） |
| H2 timer stop/resume | 機能は必要。単独の API にするか再考 | H1 に内包するのが第一候補。残すなら `hal_timer_tick_suspend()`/`hal_timer_tick_resume()` に限定。invariant TSC の有無で UNSUPPORTED を返すのは layering が変（tick を止められるかと elapsed time を測れる counter があるかは別の capability） |
| H3 notify の契約 | そのまま採用 | API の追加なし。HAL_OK を返した notify は target が online なら hal_cpu_idle() 中・hal_cpu_idle_suspend() 中・tick の停止中でも idle の wait から戻す。H4 の suspend の状態でも HAL 内部の notify の経路は mask されない。hal_cpu_stop/start（hotplug）は推奨しない |
| H4 IRQ suspend | 必要。責務を整理 | per-CPU の LAPIC の LVT（thermal・性能 counter・CMCI・LINT）は各 CPU の state なので H1 側（current CPU が自分で）。H4 は system-wide の routing（IOAPIC、kernel の IRQ、登録の無い pin）だけ。形は `int hal_irq_set_wake(int irq, bool enable)` ＋ `int hal_irq_suspend(void)` ＋ `void hal_irq_resume(void)`（Linux の enable_irq_wake と suspend_device_irqs の分離に近い）。wake list 方式も許容範囲 |
| 時刻 | counter で良い | hal_rtc_read_counter() の契約を強化:「HAL が公開する全ての CPU の idle state（hal_cpu_idle_suspend() を含む）をまたいで報告の rate で進み続ける」。suspend-safe の counter を提供できない machine は S0i3 未対応の判定（hal_timer_stop の UNSUPPORTED ではなく） |

H4 で必ず決めること: suspend の間に wake の IRQ が来た時、suspend 済みの device の通常の handler を呼ぶのか、wake の事象として latch するだけか（Linux は handler を走らせず、最初の interrupt を wake の事象として IRQ を pending/suspended にし、通常の処理は resume の後に戻す）。API の契約に明記すること。

H1 の補足: CPUID leaf 5 の sub-state は ACPI の C-state ではなく processor-specific。Linux の intel_idle は CPU の model ごとの表（Alder Lake は C1E/C6/C8/C10、C10 = MWAIT 0x60）を持つ。hal_cpu_idle(level) に変える (b) は通常の idle と system-suspend の idle の要求が違うので利点が薄い。state の表 (c) は長期には綺麗だが cpuidle の governor を作る別の project で、その時も state の ID は opaque、exit latency・target residency だけを公開する。

専門家の縮めた形:

```c
/* Enter the deepest suspend-safe idle state on the current CPU.  The HAL quiesces and restores CPU-local
 * periodic tick and suspendable private interrupt sources around the idle state.  Returns after an
 * interrupt with local IRQs disabled. */
int hal_cpu_idle_suspend(void);
/* Arm or disarm a numbered IRQ as a system wake source. */
int hal_irq_set_wake(int irq, bool enable);
/* Quiesce ordinary system IRQ delivery for system suspend. */
int hal_irq_suspend(void);
/* Restore the exact IRQ state saved by hal_irq_suspend(). */
void hal_irq_resume(void);
```

H3 は hal_cpu_notify() の契約の強化だけ。境界: kernel = device の policy・ACPI LPS0 の policy・wake の源の policy・SMP の suspend の調停・経過時間の計算。HAL = CPU-local の idle の mechanism・CPU-local の tick と LAPIC・interrupt controller の mechanism・CPU 間の wake・止まらない counter。

要約: H3 はそのまま、H1 は raw hint を捨てる、H2 は H1 に吸収、H4 は global と per-CPU を分離。
