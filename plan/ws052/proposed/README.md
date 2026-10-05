# WS052 の HAL の API の差分の案（S0i3）

**第 1 版（履歴）。専門家のレビュー（[expert-review-2026-10-05.md](expert-review-2026-10-05.md)）を受けた第 2 版が [README-v2.md](README-v2.md) にあり、
承認を求める差分はそちら（`hal-cpu-idle-suspend.diff`・`hal-cpu-notify-wake-v2.diff`・`hal-irq-wake.diff`・`hal-rtc-counter-idle.diff`）。この file と H1〜H4 の
`*.diff` は比較のために残す（2026-10-05 P4）。**

状態: 第 1 版（第 2 版に置き換えた）。2026-10-05 P1 generation17（q727、ws052-p002 の準備）。`include/hal/hal.h` は変えていない。
承認は差分ごと（AGENTS.md、Guardrail の「HAL」）。どれも宣言と契約だけで、`src/hal` の実装は承認の後に書く（今は準備のコードも無い）。

設計の背景は [design.md](../design.md) の §2（S0i3 の段）と §6（CPU の idle と timer）。S0i3（modern standby）では OS は platform に「眠れ」と
命じず、全 device を低電力にして全 CPU を最も深い C-state で止めると、PMC が platform を SLP_S0 にする。今の HAL では次の 4 つができない。

| # | 差分 | 何のため | 今の HAL でできない理由 |
| --- | --- | --- | --- |
| H1 | [hal-cpu-idle-deep.diff](hal-cpu-idle-deep.diff)（SHA256 `98dc274b…a3b`）: `int hal_cpu_idle_deep(uint32_t hint)` | CPU を最も深い C-state（ADL-P の C10、MWAIT）で止める | `hal_cpu_idle()` は `sti; hlt` で、`hlt` は C1 にしかならない。package が C10 に入らないと SLP_S0 に届かない |
| H2 | [hal-timer-stop.diff](hal-timer-stop.diff)（SHA256 `69ad65f3…ef0`）: `int hal_timer_stop(void)`・`int hal_timer_resume(void)` | S0i3 の間だけ periodic の tick を止め、wake の後に再開する | tick（LAPIC の periodic timer、`HAL_TIMER_FREQUENCY`）が毎回 CPU を起こすので C10 に留まれない。止める口が無い |
| H3 | [hal-cpu-notify-wake.diff](hal-cpu-notify-wake.diff)（SHA256 `b9398bff…f030`）: `hal_cpu_notify()` の契約の明記（API の追加なし） | wake で BSP が他の CPU（AP）を起こす | AP の停止は新しい API にせず、kernel が各 CPU の idle で H2・H1 を使い、`hal_cpu_notify_mask()` で起こす。そのため「notify は H1 の深い idle・tick の停止中・H4 の mask 中でも必ず起こす」を契約にする（今の契約は「XXX: Add explanation.」） |
| H4 | [hal-irq-suspend.diff](hal-irq-suspend.diff)（SHA256 `9302f409…95bf`）: `int hal_irq_suspend(const int *wake_irqs, unsigned count)`・`int hal_irq_resume(void)` | S0i3 の間、wake の源（SCI、wake の device の line）以外の割り込みを止め、戻す | kernel は登録した IRQ を `hal_irq_mask()` で止められるが、HAL が持つ源（LAPIC の thermal・性能 counter・CMCI・LINT の LVT、登録の無い I/O APIC の pin）は kernel から見えない。元の mask を HAL の中に保って戻す口も無い |

## 使い方（承認後の p006 の kernel の流れ）

1. device を suspend し（p004・p005）、ACPI の LPS0 の `_DSM`（display off、entry）と wake の GPE だけの有効化（p003）。
2. `hal_irq_suspend(wake_irqs)`（H4）。
3. 全 CPU の idle の loop で `hal_timer_stop()`（H2）→ `hal_cpu_idle_deep(hint)`（H1。hint は ACPI の `_CST` か LPIT の値、無ければ 0）。BSP も同じ。
4. wake: SCI か wake の device の割り込みで起きた CPU が `hal_cpu_notify_mask()`（H3）で他の CPU を起こし、各 CPU が `hal_timer_resume()`、
   kernel の時刻を `hal_rtc_read_counter()` で進める。
5. `hal_irq_resume()`、LPS0 の exit・display on、device の resume。

## 他の architecture

i386・arm64・sparcv9・m68k は S0i3 に当たる物を持たない（ベータ2 は 5330 だけ）。承認後の実装では、H1・H2・H4 は
`HAL_ERR_UNSUPPORTED` を返し（`hal_irq_set_mode` と同じ扱い）、kernel は「未対応の platform」として sleep を中止して理由を返す（§10-3 の決定）。
H3 は契約の明記だけで、各 architecture の今の notify がそれを満たすかを実装の時に確かめる。

## 当てない場合

S0i3 に入れない（package C10 に入らず、tick で起き続ける）。ACPI・device の側（p003〜p005）は HAL の差分なしで進められるが、S0i3 の入口（p006）は
H1・H2 が無いと作れない。H4 が無いと、HAL の内部の源（thermal の LVT など）で wake しうる（入れても直ぐ起きる）。H3 が無いと AP の wake は
今の実装に頼る（契約が無いので将来の変更で壊れうる）。

## 未確かめ（p002 の残り）

- 5330 の `_CST`（DSDT・SSDT）の FFixedHW の C10 の hint と、LPIT（取り出しの許可が要る、design §10-4）。
- 5330 の CPUID leaf 5 の sub-state の数（実機で読む）と invariant TSC が S0ix の間も数えるか（Intel の SDM の上では ART・TSC は S0ix で数える）。
