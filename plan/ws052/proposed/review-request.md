# HAL の API の追加の案 H1〜H4 についてのレビューのお願い（S0i3 / modern standby）

2026-10-05 Q1。差分の本文は同じ directory の `*.diff`、背景は `README.md` と `../design.md` の §2・§6。

## 背景

zedBSD は kernel と platform の間に HAL（`include/hal/hal.h`）を置き、kernel は HAL の API だけで CPU・割り込み・timer を扱う。
対象の機械（Dell Latitude 5330、Alder Lake-P）で S0i3（Windows の modern standby、Linux の s2idle に当たる物）を実装したい。

S0i3 では OS は firmware に「眠れ」と命じない（S3 のような ACPI の sleep の命令は無い）。OS が

1. 全 device を低電力にし（済み: NVMe・xHCI・HDA・LPSS-I2C・i915 の suspend、ACPI の LPS0 の `_DSM`）、
2. 全 CPU を最も深い C-state（この機械では C10、firmware の LPIT が MWAIT の hint `0x60` を示す）で止め、
3. CPU を起こす物を wake の源だけにする

と、PMC が package を C10 から SLP_S0 に落とす。2 と 3 が今の HAL ではできない。

## 今の HAL でできないこと

| | 今の HAL | S0i3 に要ること |
| --- | --- | --- |
| CPU の idle | `hal_cpu_idle()` は `sti; hlt`。`hlt` は C1 にしかならない | MWAIT で C10（hint は firmware の LPIT・`_CST` から） |
| tick | LAPIC の periodic timer（`HAL_TIMER_FREQUENCY`）が常に走り、毎回 CPU を起こす。止める口が無い | S0i3 の間だけ止め、wake の後に再開し、時刻は止まらない counter（invariant TSC、`hal_rtc_read_counter()`）で進める |
| 割り込み | kernel は自分が登録した IRQ を `hal_irq_mask()` で止められる | HAL が持ち kernel から見えない源（LAPIC の thermal・性能 counter・CMCI・LINT の LVT、登録の無い I/O APIC の pin）も止め、wake の源（ACPI の SCI、wake の device の line）だけを残し、後で元に戻す |
| 他の CPU の wake | `hal_cpu_notify()` は有るが、契約は「XXX: Add explanation.」 | 深い idle・tick の停止・mask の中でも notify が必ず CPU を起こすこと |

## 案

| # | API | 内容 |
| --- | --- | --- |
| H1 | `int hal_cpu_idle_deep(uint32_t hint)` | 今の CPU を最も深い状態で 1 つの割り込みまで待たせる。hint は firmware の名前（amd64 では MWAIT の eax）、0 なら HAL が CPUID leaf 5 から選ぶ。深い状態が無い CPU は `HAL_ERR_UNSUPPORTED`（kernel は `hal_cpu_idle()` を使う） |
| H2 | `int hal_timer_stop(void)`・`int hal_timer_resume(void)` | 今の CPU の periodic tick を止める・再開する。counter が深い idle をまたいで数えない機械（invariant TSC が無い）は `HAL_ERR_UNSUPPORTED` |
| H3 | （API の追加なし）`hal_cpu_notify()` の契約の明記 | 「H1 の深い idle、H2 の停止中、H4 の mask 中でも、notify は必ずその CPU を起こす」 |
| H4 | `int hal_irq_suspend(const int *wake_irqs, unsigned count)`・`int hal_irq_resume(void)` | wake の源以外の割り込みの源を全部止め、元の mask を HAL の中に保って戻す。MSI・MSI-X は device の側（kernel が先に device を静かにする） |

kernel の流れ（p006）: device の suspend → LPS0 の entry → `hal_irq_suspend(wake)` → 各 CPU が idle の loop で `hal_timer_stop()` → `hal_cpu_idle_deep(0x60)`
→ wake の割り込みで起きた CPU が `hal_cpu_notify_mask()` で他を起こす → 各 CPU が `hal_timer_resume()`、時刻を counter で進める → `hal_irq_resume()` → LPS0 の exit → device の resume。

i386・arm64・sparcv9・m68k は H1・H2・H4 で `HAL_ERR_UNSUPPORTED` を返し、kernel は sleep を中止して理由を返す（`hal_irq_set_mode` と同じ扱い）。

Linux の対応する物: H1 = cpuidle の最深の state（intel_idle の MWAIT）、H2 = NO_HZ と tick の停止、H4 = `suspend_device_irqs()` と wake の IRQ の再有効化、H3 = IPI による wake。

## レビューで見てほしい点（考えられる他の API）

1. **H1 の形**
   - (a) 案: 別の関数 `hal_cpu_idle_deep(hint)`。普段の idle（`hal_cpu_idle()`）は C1 のまま変えない。
   - (b) `hal_cpu_idle()` に段の引数を足す（`hal_cpu_idle(level)`）。呼び出し元が全部変わる。
   - (c) CPU の idle の状態の表を返す `hal_cpu_idle_states(...)` と `hal_cpu_idle_state(index)`。普段の idle にも深い C-state を使う道が開く（cpuidle に当たる物、待ち時間の管理が kernel に要る）。
   - hint を platform の生の値（MWAIT の eax）で渡すのが HAL の抽象として良いか。抽象の段（「最も深い」「S0ix 用」）だけにして値は HAL が firmware の table から引く方が良いか。
2. **H2 の形**
   - (a) 案: 止める・再開するだけ。S0i3 の間だけ使う。
   - (b) 一般の tickless（one-shot の deadline を設定する `hal_timer_set_deadline(t)`、periodic と one-shot の切り替え）。普段の idle の省電力にも効くが、kernel の時間の管理の作り直しが大きい。
3. **H3**: API を足さずに契約の明記だけで良いか。代わりに AP を明示に止める・起こす API（`hal_cpu_stop()`・`hal_cpu_start()`、CPU の hotplug に当たる物）にすべきか。
4. **H4 の形**
   - (a) 案: wake の IRQ の一覧を渡し、他を HAL が全部止め、HAL が元を保つ。
   - (b) HAL が持つ源を列挙する API を足し、kernel が 1 つずつ止める。HAL の内部が kernel に見える。
   - (c) H1・H2・H4 をまとめた高い段の 1 つの API（`hal_system_s0ix(wake_irqs, count)` を全 CPU が呼ぶ、または BSP だけが呼び AP は HAL が止める）。API の数は減るが、多 CPU の調停と policy が HAL に入る。
5. **時刻**: wake の後の時刻を `hal_rtc_read_counter()`（TSC）で進める前提で良いか。S0ix の間に TSC が止まる機械では H2 が `HAL_ERR_UNSUPPORTED` を返す扱いで良いか。

## 当てない場合

S0i3 に入れない（package が C10 に入らず、tick で起き続ける）。device と ACPI の側（実装済み）だけでは電力は下がらない。H4 が無いと、HAL の内部の源（thermal の LVT など）で入っても直ぐ起きうる。H3 が無いと AP の wake が今の実装の偶然に頼る。
