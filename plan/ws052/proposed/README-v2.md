# WS052 の HAL の API の差分の案 第 2 版（S0i3、専門家のレビューの後）

状態: **提案（未適用、承認待ち）**。2026-10-05 P4（Q1 の依頼: [専門家のレビュー](expert-review-2026-10-05.md)を受けて H1〜H4 を再レビューし、
追加する HAL の API の結論を出す）。`include/hal/hal.h` は変えていない。code（`src/hal`・kernel）も書いていない。
第 1 版（[README.md](README.md)、H1〜H4 の `*.diff`）は履歴として残す。この版の差分は新しい file 名で同じ directory に置く。

承認は差分ごと（AGENTS.md、Guardrail の「HAL」）。差分はどれも `include/hal/hal.h` の宣言と契約だけで、`src/hal` の実装は承認の後（p006）。
4 つの差分はそれぞれ単独で今の main（777caf8d、hal.h は c6f504c4 と同一）の hal.h に `patch -p1 --dry-run` で当たり、
下の表の順に 4 つ続けて当てても当たる（確かめた、§11）。

## 1. 結論の API の一覧

| # | 差分（SHA256） | 内容 | 第 1 版からの変更 |
| --- | --- | --- | --- |
| H1v2 | [hal-cpu-idle-suspend.diff](hal-cpu-idle-suspend.diff)（`10329d04940a37bf19ba2e69a536ef5b9135dffee71a16ab0d1f8bf39064ec68`） | **追加** `int hal_cpu_idle_suspend_supported(void)`・`int hal_cpu_idle_suspend(void)`: 今の CPU を system の suspend-to-idle に適した最も深い idle に入れ、1 つの割り込みで戻る。state は HAL が選ぶ。HAL が内部で今の CPU の tick を止めて戻し、CPU-local の private な割り込みの源（amd64 なら LAPIC の LVT）を静かにして戻す。probe の口を 1 つ足した | raw の hint の引数を消した（専門家）。H2 の tick の停止・再開を吸収した（専門家）。probe を足した（kernel が device に触る前に未対応を知るため、§4-1） |
| H3v2 | [hal-cpu-notify-wake-v2.diff](hal-cpu-notify-wake-v2.diff)（`526539eb22543820cbfaaab70c9bc74c976f178f1a22da2c3580aa6802c212df`） | **契約の明記だけ**（API の追加なし）: `hal_cpu_notify()`・`hal_cpu_notify_mask()` の「XXX: Add explanation.」を本文に。HAL_OK を返した notify は online の CPU に必ず届き、`hal_cpu_idle()`・`hal_cpu_idle_suspend()` の待ちを終え、`hal_irq_suspend()` の間も届く | 専門家の案の文を土台に、今の amd64・i386・UP の実装が返す error の値も契約に書いた |
| H4v2 | [hal-irq-wake.diff](hal-irq-wake.diff)（`005e5e6855c89a6008f2818516c22f8949a027391ce2b714eebcd2b663fbf4a8`） | **追加** `int hal_irq_set_wake(int irq, bool enable)`・`int hal_irq_suspend(void)`・`int hal_irq_resume(void)`: wake の源の IRQ を arm し、system-wide の routing（I/O APIC の pin、MSI の vector、登録の無い pin）を wake 以外全部 mask して戻す。**suspend の間に来た wake の IRQ は handler を runtime どおり呼ぶ（latch しない）** | wake の一覧の引数を `hal_irq_set_wake` に分けた（専門家）。per-CPU の LVT は H1v2 の側へ（専門家）。「handler を呼ぶか latch するか」を決めて契約に書いた（§4-4） |
| H5 | [hal-rtc-counter-idle.diff](hal-rtc-counter-idle.diff)（`dea1aaef2249143962d76da96915976c5c5c4f0e02e6b90602baf874c5a58453`） | **契約の強化だけ**（API の追加なし）: `hal_rtc_read_counter()` の counter は HAL が入る全ての idle state（`hal_cpu_idle_suspend()` を含む）をまたいで同じ rate で進む。約束できない state には HAL が入らない（probe が UNSUPPORTED） | 新規（専門家の「時刻」の指摘）。第 1 版は H2 の UNSUPPORTED で表していた |
| — | （削除）H2 `hal_timer_stop`・`hal_timer_resume` | 単独の API にしない。tick の停止・再開は H1v2 の中 | 専門家の第一候補どおり |

追加する宣言は 5 つ（H1v2 で 2、H4v2 で 3）、契約の明記・強化が 2 つ（H3v2、H5）。第 1 版は追加 5（H1 で 1、H2 で 2、H4 で 2）・明記 1 だった。

当てる順（続けて当てる時）: H1v2 → H3v2 → H4v2 → H5。単独ならどれも順不同で当たる。

## 2. 境界（専門家の 2 層の整理をこの tree の言葉で）

| 層 | 持ち物 | zedBSD の今の実体 |
| --- | --- | --- |
| **HAL（mechanism）** | CPU-local の idle の mechanism（state の選択を含む）、CPU-local の tick と private な割り込みの源、割り込み controller の mechanism（routing・mask・wake の arm）、CPU 間の wake（notify）、止まらない counter | `src/hal/amd64/task.c` の `hal_cpu_idle`（`sti; hlt; cli`）、`bsp-pcat/lapic.c`（timer の start/stop、notify の ICR、LVT）、`irq.c`・`bsp-pcat/ioapic.c`（`irq_service[]` の masked/mode、I/O APIC の route）、`bsp-pcat/timecounter.c`（TSC） |
| **kernel（policy）** | device の suspend/resume の順と中止、ACPI の LPS0 と wake の GPE、どの IRQ が wake の源か、SMP の suspend の調停（誰が入る・誰が起こす・誰が判定する）、経過時間の計算と時刻の進め方、spurious な wake の判定と再突入、user の停止と再開、`/dev/system` の口と事象 | p003 `acpi-sleep.c`・`acpi-event.c`、p004 `pci-power.c`、p005、`system-device.c` の `KERN_SYSTEM_SLEEP`、`sched.c` の idle loop、`clock.c` の `kernel_ticks` |

HAL は「何が起きたか」を kernel に通知する口を持たない（wake の理由の判定は kernel の仕事: §7-4）。HAL は policy を持たない（どの CPU から入るか、何回 spurious を許すか、は kernel）。

## 3. 各 API の契約（全文は diff の英文。ここは日本語の要点と根拠）

### H1v2 `hal_cpu_idle_suspend_supported()` / `hal_cpu_idle_suspend()`

- `hal_cpu_idle_suspend()` は `hal_cpu_idle()` と同じ呼び方（割り込みを許して待ち、戻る時は禁止。待ちを終えた割り込みの handler は返る前に済んでいる）。
  違いは入る state の深さと、**HAL が内部で今の CPU の tick を止め、CPU-local の private な源を静かにし、戻る前に両方を元に戻す**こと。
  その間この CPU で `kernel_timer_handler()` は呼ばれない。kernel は `hal_rtc_read_counter()`（H5 で止まらないことを約束）で時刻を進める。
- state の選択は HAL（kernel は state の名前を知らない。専門家の指摘 1）。amd64 の選び方は §6-1。
- `hal_cpu_idle_suspend_supported()` は、全 CPU に suspend-safe な idle の state があり、counter がそれをまたいで進む時に HAL_OK。
  それ以外は HAL_ERR_UNSUPPORTED で、その時 `hal_cpu_idle_suspend()` は待たずに同じ値を返す。`hal_cpu_start_others()` の後は答えが変わらない。
  **なぜ probe が要るか**: kernel の流れ（§7）は device を全部 suspend した後で CPU の idle に入る。入れないことを device に触った後で知るのは
  遅い（全部 resume して中止になる。QEMU の「未対応の platform で安全に失敗」も、device を一往復させた上で失敗することになる）。
  `KERN_SYSTEM_SLEEP_INFO`（design §7）の「対応の有無」にも要る。1 つの bool の問い合わせで足り、state の表（専門家が将来の cpuidle に残した案 (c)）は作らない。
- 返り値: HAL_OK（待った後）/ HAL_ERR_UNSUPPORTED（待たずに）。HAL_ERR_INVALID は無い（引数が無い）。
- `hal_cpu_notify()` は必ずこの待ちを終える（H3v2）。

### H3v2 `hal_cpu_notify()` / `hal_cpu_notify_mask()` の契約

- 「HAL_OK を返した notify は online の target に必ず届く: `hal_cpu_idle()` の中でも、`hal_cpu_idle_suspend()` がどの state に入っていても、
  `hal_irq_suspend()` が他の源を mask している間でも。kernel が system sleep から全 CPU を戻す手段であり、他の HAL の操作はこれを mask しない」。
- error の値も書いた（今の実装が返す物をそのまま契約に）: HAL_ERR_INVALID（無い CPU）、HAL_ERR_STATE（online でない: amd64 `smp.c` の `target->ready`、
  i386 `smp.c` の `cpus[cpu].ready`）、HAL_ERR_UNSUPPORTED（CPU 間の割り込みの手段が無い machine: `src/hal/cpu-up.c` の UP の実装、i386 の PIC mode）。
  `hal_cpu_notify_mask()` は全 target を検査してから送る（amd64 `smp.c:344`〜: 検査の loop の後で送る loop。error なら 1 つも送っていない）。
- **今の実装が満たすか**（amd64）: `amd64_lapic_notify()` は ICR で fixed の vector `AMD64_VECTOR_NOTIFY` を送る（`lapic.c`）。MWAIT は fixed の割り込みで
  必ず抜ける（割り込みが禁止でも ECX bit 0 の interrupt break event で抜ける）。LAPIC の timer の停止や LVT の mask は ICR の受信に関係しない。
  `hal_irq_suspend()`（H4v2）は I/O APIC と `irq_service[]` だけに触り、IPI の vector（notify・TLB）には触らない契約。だから満たす。
  i386（APIC mode）も同じ構造（`i386_lapic_send_fixed`）。arm64・sparcv9・m68k は UP（`cpu-up.c`）で UNSUPPORTED を返すので契約の対象外（HAL_OK を返さない）。

### H4v2 `hal_irq_set_wake()` / `hal_irq_suspend()` / `hal_irq_resume()`

- `hal_irq_set_wake(irq, enable)`: IRQ を system sleep の wake の源として arm/disarm。設定は変えるまで保たれ、何回の suspension もまたぐ。runtime には何もしない。
  IRQ 自身の mask（`hal_irq_mask`）とは別: arm していても kernel が mask していれば起こさない。numbered も MSI も受ける。
  返り値: HAL_OK / HAL_ERR_INVALID（無い IRQ）/ HAL_ERR_STATE（`hal_irq_suspend()` の最中）/ HAL_ERR_UNSUPPORTED（1 つだけ届けたまま他を mask できない controller）。
- `hal_irq_suspend()`: HAL が system-wide に route する全ての源（登録の有無を問わず numbered・MSI）を、arm された IRQ を除いて mask する。
  **触らない物**: CPU-local の源（各 CPU の tick と private な源。`hal_cpu_idle_suspend()` が自分の CPU で扱う）と、CPU 間の notification
  （`hal_cpu_notify()`、HAL 自身の cross-CPU の操作 = amd64 の TLB shootdown の IPI）。
  **wake の IRQ が suspend 中に来たら、runtime どおり route 先の CPU で handler を呼ぶ。後に取っておかない**（§4-4 で決めた）。だから wake の IRQ の
  handler は「device が suspend 中でも走ってよい物」でなければならず、ACPI の SCI の handler はそう（§4-4）。
  mask された IRQ が来ても（device がまだ message を送る）、ack して捨てる。保持しない。
  suspension の間の `hal_irq_mask()`・`hal_irq_unmask()`: mask 中の IRQ に対しては `hal_irq_resume()` が戻す内容だけを変える、arm された IRQ に対しては直ちに効く。
  1 度に 1 つの suspension。返り値: HAL_OK / HAL_ERR_STATE（既に suspend 中）/ HAL_ERR_UNSUPPORTED（できない platform）。error なら何も変えていない。
- `hal_irq_resume()`: `hal_irq_suspend()` が変えた全ての源を、kernel が最後に求めた状態（届ける/mask）に戻す。返り値: HAL_OK / HAL_ERR_STATE（suspend 中でない）。
  int にした理由は §5。

### H5 `hal_rtc_read_counter()` の契約の強化

- 今の契約（epoch は不定、差だけ意味がある、周波数は boot の間一定、linearizable）に 1 文を足す:
  「counter は HAL がどの CPU で入るどの idle state（`hal_cpu_idle()`・`hal_cpu_idle_suspend()` を含む）もまたいでその周波数で進む。ある state でこれを
  約束できない HAL はその state に入らない（`hal_cpu_idle_suspend_supported()` が HAL_ERR_UNSUPPORTED を言う）。kernel は system sleep の後に
  tick の数をこの counter から進める」。
- 「counter が suspend-safe でない machine の判定」は**新しい口を作らず**、H1v2 の probe に畳む（専門家: tick を止められるかと counter の有無は別の
  capability だが、kernel が要る判定は「S0i3 に入れるか」の 1 つ）。counter の約束は無条件（HAL がその責任で state を選ぶ）、入れるかは probe。

## 4. 専門家の指摘への回答

### 4-1. H1: raw の hint を消して `hal_cpu_idle_suspend(void)` にするか → **する**。state は HAL が firmware の LPIT と CPUID から選ぶ

- hint（MWAIT の eax）は x86 の実装の手順であり、HAL の抽象に出すべきでない（専門家）。消す。
- **HAL が state を選ぶ方法（amd64）**: HAL は既に ACPI の静的な table を自分で読む（`src/hal/amd64/bsp-pcat/acpi.c`: RSDP → XSDT/RSDT → MADT・MCFG を
  `find_sdt()` で署名から探す）。**LPIT**（署名 `LPIT`）も静的な table で AML は要らないので、同じ経路で HAL が読む。LPIT の native C-state の entry の
  entry trigger が FFixedHW（GAS の address space 0x7F）なら、その address field が MWAIT の hint（5330 では 0x60 = C10、p002 で読んだ）。
  これが「firmware が S0ix の入口として指定する state」であり、Intel の CPU の model ごとの表（Linux の intel_idle が持つ物）を HAL に持たなくてよい。
  CPUID leaf 5 は capability の確認に使う（EDX の nibble `(hint >> 4)` が 0 でなく、sub-state `(hint & 0xF)` がその数より小さい; ECX bit 0 の拡張と bit 1 の
  interrupt break event; CPUID.1 ECX bit 3 の MONITOR/MWAIT）。CPUID leaf 5 だけから「最も深い」を選ぶ案は採らない（専門家の補足: processor-specific で
  ACPI の C-state と対応しない）。
- **kernel が ACPI から得た値を HAL に渡す口は作らない**。`_CST` は AML（動的、5330 では hint は CPU NVS から実行時に入る）で kernel の interpreter が要るが、
  LPIT で足りる platform（S0ix を申告する Intel の platform は LPIT を持つのが通例。Windows が residency の報告に使う）だけを対象にする。LPIT が無く `_CST` だけの machine は
  probe が UNSUPPORTED（将来要れば、kernel → HAL の片方向の設定の口を別の差分として検討する。今は作らない。§5）。
- FADT の `LOW_POWER_S0_IDLE_CAPABLE`（bit 21）は platform の policy（S0ix の申告）なので **kernel の ACPI の側が見る**（kernel は FADT を既に持つ:
  `acpi-kern.c` の `firmware.fadt`）。HAL の probe は CPU の mechanism の事実（MWAIT・LPIT の hint・invariant TSC）だけを見る。kernel の「対応の有無」=
  probe が HAL_OK かつ FADT bit 21 かつ LPS0 device がある。
- 専門家の (b) `hal_cpu_idle(level)` は採らない（通常の idle と system suspend の idle の要求が違う。呼び出し元が全部変わる）。(c) state の表は cpuidle の
  governor を作る別の project（専門家の判断に同意。その時も state の ID は opaque にする）。
- 追加で probe を 1 つ足した理由は §3 の H1v2。

### 4-2. H2: H1 に吸収するか → **する**。kernel の scheduler の側で要ること

- `hal_timer_stop/resume` を単独の API にしない。`hal_cpu_idle_suspend()` が今の CPU の tick を止め、戻る前に再開する（amd64 には既に
  `amd64_lapic_timer_stop()`・`amd64_lapic_timer_start()` が `lapic.c` にあり、`timer_initial` は BSP で 1 度の較正なので再開は MMIO の 3 write）。
  kernel は「tick が止まった CPU」を `hal_cpu_idle_suspend()` の呼び出しの中でしか持たない。invariant は単純になる。
- **tick が止まった間の扱い**: `kernel_ticks` は CPU 0 の tick だけが進める（`clock.c:82`）。全 CPU が suspend idle に入ると `kernel_ticks` は止まる。
  これは **意図どおり**（Linux の s2idle も全 CPU が入ると `tick_freeze()` → `timekeeping_suspend()` で timer と timekeeping を止め、timer は wake まで
  発火しない。S0i3 の間に timer で起きるのは RTC の alarm のような wake の源だけ）。kernel の timer（waitq の deadline、process timer）は wake まで寝る。
- **wake の後の時刻の進め方**: CPU 0 の idle loop が `hal_cpu_idle_suspend()` の前後で `hal_rtc_read_counter()` を読み、差を tick に換算して `kernel_ticks` に
  足す（端数の counter は次回に持ち越す）。`clock.c` に `kern_clock_idle_suspend_begin()/end()` に当たる関数を p006 で足す。CLOCK_REALTIME は monotonic +
  offset なので一緒に正しくなる。deadline の比較は `sched.c:888` の `wakeup_tick <= now`、`waitq.c:220` の `sched_ticks() >= deadline` で、飛びに耐える。
  `process_timer_tick(now)`（`timer.c`）の expiry の比較が `<=` であることを p006 で確かめる（`timer.c:406` は `expiry = deadline` を置くだけなので比較の場所を見る）。
- CPU 0 が最初に起きる CPU でない時: 他の CPU が先に戻って動く間 `kernel_ticks` は古いまま（timeout が長くなる方向、短くならない）。coordinator の
  notify（§7-4）で CPU 0 が直ちに戻り補正する。wake の IRQ の affinity を CPU 0 にしておけば（§7-3）、普通は CPU 0 が最初。
- tick 以外: `kern_random_tick`・`sched_clock_cpu` の `scheduler_ticks` は CPU 0 の次の tick で追いつく（1 tick の遅れ）。
- 一般の tickless（one-shot の deadline、専門家の案 (b)）は作らない。普段の idle の省電力は別の目標（Future Work、§5）。

### 4-3. H3: `hal_cpu_notify()` の契約 → **専門家の文を採用し、今の実装が満たすことを確かめた**（§3 の H3v2）

- `hal_cpu_stop/start`（hotplug）は作らない（専門家に同意。Linux の s2idle も CPU を offline にしない）。
- 契約に error の値を足した理由: kernel の p006 が「notify できない CPU」を SMP の調停の失敗として扱えるように。`sched.c:1971` の `notify_cpu()` は
  error を HAL_FATAL にしているので、p006 の coordinator は `hal_cpu_notify_mask()` の返り値を自分で見る。

### 4-4. H4: per-CPU の LVT を H1 側へ、H4 は system-wide の routing だけ → **する**。形は `hal_irq_set_wake` ＋ `hal_irq_suspend(void)` ＋ `hal_irq_resume(void)`。**wake の IRQ は handler を呼ぶ（latch しない）**

形:

- wake の一覧の引数（第 1 版）より `hal_irq_set_wake` を採る。理由: (1) wake の源の所有者が device の driver と ACPI の driver で別々（p004 の
  `drv_pci_device_set_wake()` が PME と platform の wake を有効にする所で、自分の MSI が wake なら `kern_irq_set_wake()` を呼べる。SCI は ACPI の driver が
  boot で 1 度 arm する）。一覧を kernel の 1 箇所で集めるより所有が合う。(2) Linux の `enable_irq_wake()` と `suspend_device_irqs()` の分離と同じで、
  読む人に馴染む。(3) suspension を繰り返す時（spurious wake の再突入は hal_irq_suspend を保ったまま行うので実は繰り返さないが）、設定が残る。
- per-CPU の LVT（amd64: thermal 0x330・性能 counter 0x340・CMCI 0x2F0・LINT0 0x350・LINT1 0x360・error 0x370）は各 CPU の state なので
  `hal_cpu_idle_suspend()` が自分の CPU で保存・mask・復元する（専門家）。`hal_irq_suspend()` は I/O APIC の pin（今の HAL は ISA の 16 route）と MSI の
  vector（`irq_service[]` の software の mask: amd64 の `hardware_mask()` は MSI に no-op で、masked の到着は `irq_handler()` が EOI して捨てる
  `irq.c:647`）だけ。tick（logical IRQ 0 = `IRQ_TIMER`、`hal_irq_mask` も触らない）と IPI の vector には触らない。

**suspend の間に wake の IRQ が来た時 — handler を呼ぶか latch するか**: **呼ぶ**。

- Linux の扱い（記憶に基づく要約、code は写していない）: `suspend_device_irqs()` は `IRQF_NO_SUSPEND` でない IRQ を全部 disable し、`enable_irq_wake()` された
  IRQ は `IRQD_WAKEUP_ARMED` を立てて enable のまま残す。armed の IRQ が来ると `irq_may_run()` → `irq_pm_check_wakeup()` が **handler を走らせず**、IRQ を
  disable して `IRQS_PENDING|IRQS_SUSPENDED` を立て、`pm_system_irq_wakeup(irq)` で wake を知らせる。`resume_device_irqs()` が enable し直す時に pending を
  resend して handler が走る。s2idle では SCI も wake の IRQ で、`acpi_s2idle_wake()` が GPE の status を直接読んで（`acpi_ec_dispatch_gpe()`）本物の wake か
  判定し、違えば `rearm_wake_irq(acpi_sci_irq)` で戻って寝る。
- zedBSD で latch を採らない理由:
  1. **p003 の SCI の handler が既に「latch の層」である**。`sci_interrupt()`（`acpi-kern.c:659`）→ `drv_acpi_sci_interrupt()` は PM1 と GPE の status を読んで
     **fired を記録し mask する**だけで、処理は event の thread に渡す。sleep 中は `events.sleeping` で最初の GPE を `woken_gpe` に残す
     （`drv_acpi_events_sleep_end(&woken)` で kernel が読む）。device（ACPI の register）は suspend されないので handler は sleep 中に走ってよい。
     latch を HAL に重ねると、同じ「最初の割り込みを記録して mask し、後で処理」が 2 層になる。
  2. **この HAL の dispatch に pending/resend の機構が無い**。latch するなら HAL に IRQ ごとの pending bit と、`hal_irq_resume()` での再送（edge の MSI は
     線が残らないので software で handler を呼ぶ。どの文脈で呼ぶかの契約が要る）を足す。level の SCI なら unmask で線が残っているので再発火するが、
     MSI の wake（将来）では失われる。HAL の複雑さが増え、契約も長くなる。
  3. **spurious な wake の判定は kernel の policy で、handler が走る方がやりやすい**。EC は蓋・電源ボタン・AC 以外にも query（battery の状態など）で SCI を
     上げる。本物の wake か判定するには EC の query を処理しなければならず、それは ACPI の event の thread の仕事（kernel）。latch 方式では Linux と
     同じく「unmask → 処理 → 再 arm」の往復が要る。handler を呼ぶ方式では SCI の handler → thread がいつもどおり動き、kernel の coordinator が
     結果（どの GPE が、どの Notify が来たか）を見て判定する（§7-4）。
  4. **5330 の wake の源は実質 SCI だけ**。xHCI・PCIe の PME・AWAC（RTC alarm）・LID・PBTN・EC は全部 `_PRW` の GPE → SCI（p002 の表）。D3hot の device は
     MSI を出さない。だから「wake の IRQ の handler は suspend 中に走れること」という規則の対象は今は SCI だけで、規則を満たす。将来 device の MSI を wake に
     する時は、その driver の handler が「device が D3 でも走れる」（register に触らず記録だけ）ことを driver に課す（p006 の規則として書く）。
- 契約に書いた文: 「armed な IRQ が suspend 中に来たら runtime どおり route 先の CPU で handler に dispatch する。後に取っておかない。だから handler は
  device が suspend 中でも走れる物でなければならない（ACPI の SCI の handler はそう）。mask された IRQ が来ても ack して捨てる」。
- 「どの IRQ が起こしたか」を HAL が報告する口は作らない。wake の理由は kernel が ACPI（`woken` の GPE、Notify の種類、PM1 の固定の event）から得る。
  IRQ の番号は理由として粗すぎる（SCI 1 本に全部乗る）。

### 4-5. 時刻: `hal_rtc_read_counter()` の契約の強化と「suspend-safe でない machine の判定」→ **契約を強化（H5）。新しい問い合わせの口は作らず、H1v2 の probe に畳む**

- 専門家の文を採用（§3 の H5）。counter の約束は無条件にし、約束できない state には HAL が入らない（probe が UNSUPPORTED）。
- amd64 の今の実装との整合: counter は TSC（`bsp-pcat/timecounter.c`）。`timecounter-policy.c:39` は CPUID の周波数に invariant TSC（CPUID 0x80000007 EDX bit 8）
  を要るが、KVM では invariant の bit が隠れるので PIT で較正した TSC に落ちる（`timecounter.c:162` の comment）。だから **counter が「ある」ことと
  「invariant である」ことは今も別に扱われている**（専門家の指摘どおり）。probe は invariant TSC を要件にする（§6-1）。QEMU/KVM では invariant が無く
  MWAIT も LPIT も無いので probe は UNSUPPORTED → design §8 の「QEMU では未対応で安全に失敗」。
- Intel の SDM 上、invariant TSC は全ての C-state で進む。S0ix（package C10 + SLP_S0）で TSC が進むかは SDM の文面だけでは断定できないので、
  **実機の確認を p006 の受け入れに入れる**: sleep の前後で `hal_rtc_read_epoch_time()`（CMOS の RTC、秒）と counter の差を比べ、2 秒より食い違えば
  log に出す（kernel の sanity check として残す。食い違う machine では RTC の秒で補正する fallback を p006 で判断）。根拠は SDM の invariant TSC の文
  （全ての ACPI の P-・C-・T-state で一定の rate で進む。S0i3 は OS から見ると CPU が C10 に居続ける状態）と、CPUID leaf 0x15 の ART との比（TSC は
  always-running timer から再構成できる）。**Linux は根拠にならない**: Linux の s2idle は `tick_freeze()` で timekeeping を止め、戻る時の経過時間は Core の
  CPU では TSC でなく RTC（persistent clock、`CLOCK_SOURCE_SUSPEND_NONSTOP` は Atom の S3 用）から入れているので、TSC が S0ix で進むことを Linux は
  要求していない。だから実機の確認を必須にし、食い違えば H5 を弱める差分（counter は architecture の約束の範囲で進む、kernel が RTC で照合する）を
  改めて出す。

### 4-6. 全体: 2 層の整理に沿った kernel の p006 の流れ → §7 に書き直した

## 5. 採らなかった案と理由

| 案 | 理由 |
| --- | --- |
| H1 `hal_cpu_idle_deep(uint32_t hint)`（第 1 版） | x86 の MWAIT の値が API に出る。kernel が `_CST`・LPIT を読んで渡す構造は policy と mechanism を kernel にまたがせる。HAL が LPIT を読めるので要らない |
| `hal_cpu_idle(level)`（専門家の (b)） | 通常の idle と system suspend の idle は要求が違う（残り時間・latency の管理の有無）。全呼び出し元が変わる |
| idle state の表 `hal_cpu_idle_states()`（専門家の (c)） | cpuidle の governor を作る別の project。S0i3 には 1 つの state で足りる。その時も state の ID は opaque |
| H2 `hal_timer_stop/resume` を単独の API に残す | kernel に「tick が止まった CPU」という状態が露出し、再開を忘れる・順を誤る余地ができる。H1v2 の中なら HAL の中で対になる。残すなら `hal_timer_tick_suspend/resume` に名を限る（専門家）が、今は要らない |
| 一般の tickless `hal_timer_set_deadline(t)` | kernel の時間の管理の作り直し（timer wheel・NO_HZ に当たる物）。S0i3 の範囲外。普段の idle の省電力は Future Work に |
| `hal_timer_stop` の UNSUPPORTED で「counter が suspend-safe でない」を表す（第 1 版） | layering が変（専門家）。counter の約束は H5 で無条件に、入れるかは probe に |
| counter の suspend-safety の別の問い合わせ（`hal_rtc_counter_is_nonstop()` など） | kernel が要る判定は「S0i3 に入れるか」の 1 つで、probe に畳める。口が増えるだけ |
| `hal_system_s0ix(wake_irqs, count)`（1 つの高い API、専門家の案 (c)） | 多 CPU の調停・spurious wake の再突入・wake の理由の判定という policy が HAL に入る。専門家も推奨しない |
| `hal_cpu_stop()`・`hal_cpu_start()`（hotplug） | S0i3 に CPU の offline は要らない（Linux の s2idle も offline にしない）。AP も idle の loop で `hal_cpu_idle_suspend()` に入り、notify で戻る |
| H4 を wake の一覧の引数 `hal_irq_suspend(const int *wake_irqs, unsigned count)`（第 1 版） | wake の源の所有者が driver ごとに別。`hal_irq_set_wake` なら所有が合い、Linux の分離と同形。一覧方式も「許容範囲」（専門家）だが採らない |
| HAL が持つ源を列挙する API を足し kernel が 1 つずつ止める（第 1 版の (b)） | HAL の内部（LVT・pin）が kernel に見える。per-CPU の LVT は H1v2 の中、system-wide は H4v2 の中に閉じる方が境界に合う |
| wake の IRQ を HAL が latch する（Linux の `IRQD_WAKEUP_ARMED`） | §4-4 の 1〜4。この tree では SCI の handler が latch の層、HAL に pending/resend の機構が無い、spurious の判定は kernel の policy |
| HAL が「どの IRQ が起こしたか」を報告する（`hal_irq_resume(int *woken)` など） | SCI 1 本に全部の wake が乗るので IRQ の番号は理由にならない。kernel は ACPI から理由を得る |
| `void hal_irq_resume(void)`（専門家の縮めた形） | 「suspend していないのに resume」は kernel の bug で、黙って無視するより HAL_ERR_STATE で返す方がこの tree の流儀（`hal_irq_set_mode`・`hal_space_*` は int）。kernel が error に対してできることは無いが、log には出せる。小さな差なので承認の時に void でよいと言われればそれに従う |
| kernel が `_CST` の hint を HAL に渡す片方向の口（`hal_cpu_idle_suspend_configure(...)`） | LPIT の無い machine 用。5330 には要らない。要る machine が現れた時に別の差分で |
| HAL が FADT の bit 21 と LPS0 の有無も見る | platform の policy。kernel の ACPI の driver が既に FADT を持ち LPS0 を attach している（p003）。HAL は CPU の事実だけ |

## 6. 承認後の実装の方針（p006。code はまだ書かない）

### 6-1. amd64

- **probe**（`hal_cpu_idle_suspend_supported`）: boot の `prekern_amd64_acpi_discover()` の経路で LPIT を `find_sdt("LPIT")` で探し、最初の有効な
  native C-state の entry（flags の bit 0 = disabled でない）の entry trigger が FFixedHW ならその address を hint として `struct amd64_acpi_info` に保つ。
  probe は HAL_OK を「hint がある ∧ CPUID.1 ECX[3]（MONITOR/MWAIT）∧ CPUID.5 ECX[0]・ECX[1] ∧ CPUID.5 EDX の nibble が hint を含む ∧ BSP の timecounter の
  metadata が invariant TSC（`bsp_metadata.frequency.invariant_tsc_supported`）∧ 全 AP の metadata も同じ（既存の AP の検証に乗せる）」の時だけ返す。
  AP が揃う前（`hal_cpu_start_others()` の前）は HAL_ERR_STATE でなく、BSP の事実だけで答えてよい（契約は「start_others の後は変わらない」）。
- **`hal_cpu_idle_suspend`**: (1) LVT（CMCI・thermal・性能 counter・error と、delivery mode が fixed の LINT0/LINT1）の値を保存して mask bit を立てる
  （NMI・ExtINT の LINT は触らない）。(2) `amd64_lapic_timer_stop()`。
  (3) `monitor`（per-CPU の line）→ `mwait eax=hint, ecx=1`（割り込みを禁止したまま interrupt break event で抜ける。Linux・FreeBSD の流儀）→
  `sti; nop; cli`（STI の 1 命令の遅れの後に pending の割り込みを取り、handler が走ってから戻る。`sti; mwait; cli` で `sti; hlt; cli` と同形にする案も可。
  どちらも「戻る時に割り込みの handler は済んでいる」の契約を満たす。p006 で 1 つに決める）。(4) `amd64_lapic_timer_start()`（較正済みなので 3 write）。
  (5) LVT を戻す。probe が UNSUPPORTED なら (1)〜(5) をせずに返す。
- **`hal_irq_set_wake`**: `irq_service[irq].wake` の bit。`valid_irq()` で INVALID、suspension 中は STATE。
- **`hal_irq_suspend`**: `irq_service[]` を lock しながら、`IRQ_TIMER` 以外の全 logical IRQ について「suspend 前の masked」を保存し、wake でない物を
  `masked = 1` にして I/O APIC の pin は `amd64_ioapic_mask()`。MSI は software の mask だけ（到着は `irq_handler()` の既存の経路で EOI して捨てる）。
  登録の無い pin は boot で masked のまま（`prekern_irq_init()`）なので変わらない。suspension 中の `hal_irq_mask/unmask` は保存した値を更新する
  （wake なら今の値も）。`hal_irq_resume` は保存した値を書き戻す。
- **他の architecture の stub**: `src/hal/cpu-up.c` のように arch を問わない 1 つの file（例 `src/hal/idle-suspend-unsupported.c`）に 5 つの関数を置き、
  amd64 以外の全 platform の `vmunix.mk` に足す。`hal_cpu_idle_suspend_supported`・`hal_cpu_idle_suspend`・`hal_irq_set_wake`・`hal_irq_suspend` は
  HAL_ERR_UNSUPPORTED、`hal_irq_resume` は HAL_ERR_STATE。i386 は今 build も試験もしない（Guardrail 2026-10-02）が、kernel が参照するので同じ stub を
  `vmunix.mk` に入れておく。H3v2・H5 は契約の明記で実装の変更は無い（arm64・sparcv9・m68k の notify は UP で UNSUPPORTED、契約の対象外）。

### 6-2. kernel 側（HAL の差分ではないが p006 の範囲）

- `clock.c`: `kern_clock_idle_suspend_begin()/end()`（CPU 0 だけ。counter の差を tick に換算して `kernel_ticks` に足す。端数は持ち越す）。
- `sched.c` の `sched_idle()`: runnable が無く sleep の state が ENTER の時に `hal_cpu_idle()` の代わりに `hal_cpu_idle_suspend()`（§7-4）。
- `kern/irq.c`: `kern_irq_set_wake()`（HAL への素通し、既存の `kern_irq_set_mode` と同形）。
- `acpi-kern.c`: boot で SCI を `kern_irq_set_wake(sci, true)`。sleep 中に処理した事象の種類を coordinator に返す口（§7-4）。
- user の process の停止（freeze）: `src/kern/process.c`・`signal.c` に freeze の機構は見当たらなかった（grep `freeze|frozen`）。p006 の範囲として要る
  （stop request の機構の上に作るか別に作るかは p006 の設計）。**Q1 に報告する（計画に無い依存の候補）**。

## 7. kernel の p006 の流れ（書き直し）

### 7-1. 状態

`sleep_state`: `NONE` → `ENTER`（全 CPU が suspend idle に入るべき）→ `WAKE`（起きた。coordinator が理由を判定中）→ `ENTER`（spurious なら戻る）… → `NONE`。
coordinator は `KERN_SYSTEM_SLEEP`（mode = S0 idle、root だけ）を呼んだ thread T（sessiond、p007）。kernel は自分から入らない（§10-2）。

### 7-2. 入口（T）

1. 対応の確認: `hal_cpu_idle_suspend_supported() == HAL_OK` ∧ FADT bit 21 ∧ LPS0 device（p003）。違えば EOPNOTSUPP（device に触らない）。別の sleep が
   動いていれば EBUSY（既存の `system_sleeping`）。
2. `power.sleep.begin` の事象（WS132）。user の process の停止（T 自身を除く）。
3. device の suspend（p004 `drv_pci_suspend_all`、p005 の止めて入る経路）。失敗なら resume して中止、原因の device を返す（§10-3）。
4. LPS0 の entry（p003 `drv_acpi_lps0_enter`: display off → entry）。wake の GPE だけ有効（`drv_acpi_events_sleep_begin()`）。
5. wake の IRQ の affinity を CPU 0 に（`hal_irq_set_affinity`。CPU 0 が最初に起きて時刻を補正するため。必須ではない）。`hal_irq_suspend()`。
6. 入る前の counter と RTC の秒を記録（SLP_S0 の residency の counter（LPIT state 1 の MMIO、kernel が LPIT を読む: `KERN_SYSTEM_SLEEP_INFO` 用）も）。

### 7-3. 全 CPU が入る

7. T: `sleep_state = ENTER`、`hal_cpu_notify_mask(他の全 CPU)`（`hal_cpu_idle()` で寝ている CPU に state を見させる。H3v2）。T は waitq で `sleep_state != ENTER`
   を deadline 無しで待つ → T の CPU も idle になる。
8. 各 CPU の `sched_idle()`: runnable が無く `sleep_state == ENTER` なら、CPU 0 は `kern_clock_idle_suspend_begin()`、`hal_cpu_idle_suspend()`
   （HAL が tick を止め、LVT を静かにし、C10 へ。全 CPU が入ると PMC が SLP_S0 へ）、戻ったら CPU 0 は `kern_clock_idle_suspend_end()`。
   戻ったのは「何かの割り込み」（wake の IRQ の handler は済んでいる。または notify）。`cmpxchg(sleep_state, ENTER → WAKE)` に成功した CPU が T を起こす
   （waitq の wake）。その後は普通の idle loop（`hal_cpu_idle()`、tick は動く）。
   `hal_cpu_idle_suspend()` が UNSUPPORTED を返したら（probe 済みなので起きない）`WAKE` にして理由 = error。

### 7-4. 起きた後の判定（T）

9. T が起きる: `hal_cpu_notify_mask(他の全 CPU)`（suspend idle の中に残る CPU を戻し tick を再開させる。IRQ は CPU 0 に向いているので AP は notify 以外では
   起きない）。
10. 理由の判定: ACPI の event の thread の処理の完了を待ち（p006 で `acpi-kern.c` に「sleep 中に処理した事象」の記録と、処理の完了を待つ口を足す）、
    - 蓋（LID0 の Notify 0x80 → `_LID` の値）、電源ボタン（PBTN の Notify 0x80 か PM1 の固定の event）、EC の query が生んだ wake の Notify、AWAC（RTC alarm）、
      xHCI・PCIe の PME の GPE（USB の keyboard、AC は EC の query）→ **本物の wake**。理由を記録して 11 へ。
    - 何も無い（EC の battery の query だけ、notify の IPI だけ、割り込みの理由が無い）→ **spurious**。連続の回数と総時間を数え、上限（p006 で決める。例
      連続 10 回か 30 秒）を超えなければ 7 へ戻る（`hal_irq_suspend()` も LPS0 の entry も device も保ったまま。user は停止のまま）。超えたら理由 = spurious で 11 へ。
    この間 user の process は止まっているが kernel の thread は普通に動く（tick が動くので EC の transaction の timeout も働く）。

### 7-5. 出口（T）

11. `hal_irq_resume()`、`drv_acpi_events_sleep_end(&woken)`（runtime の GPE に戻す）、LPS0 の exit（exit → display on）、device の resume
    （`drv_pci_resume_all`）、user の process の再開、`power.sleep.end reason=…`。SLP_S0 の residency の差と counter/RTC の食い違いの検査（§4-5）を
    `KERN_SYSTEM_SLEEP_INFO` と log に。`sleep_state = NONE`、`system_sleeping` を下ろす。

### 7-6. この流れが HAL に求める物（= 差分の対応）

| 段 | HAL |
| --- | --- |
| 1 | `hal_cpu_idle_suspend_supported()`（H1v2） |
| 5 | `hal_irq_set_wake()`（boot で SCI。H4v2）、`hal_irq_set_affinity()`（既存）、`hal_irq_suspend()`（H4v2） |
| 7・9 | `hal_cpu_notify_mask()` が idle・suspend idle・IRQ の suspension の中でも届く（H3v2） |
| 8 | `hal_cpu_idle_suspend()`（tick の停止と再開、LVT、MWAIT。H1v2）、`hal_rtc_read_counter()` が止まらない（H5） |
| 10 | wake の IRQ の handler が runtime どおり呼ばれる（H4v2 の契約。SCI の handler → ACPI の thread） |
| 11 | `hal_irq_resume()`（H4v2） |

## 8. 他の architecture の扱い

- i386・arm64（rpi4）・sparcv9（sun4u）・m68k（x68k）は S0ix に当たる物を持たない（ベータ2 は 5330 だけ）。§6-1 の stub で `hal_cpu_idle_suspend_supported()`
  が UNSUPPORTED を返し、kernel は段 1 で EOPNOTSUPP（device に触らない）。`hal_irq_set_mode` の UNSUPPORTED と同じ扱い。
- H3v2 の契約は HAL_OK を返す実装にだけかかる: UP の `cpu-up.c` と i386 の PIC mode は HAL_OK を返さないので対象外。i386 の APIC mode は amd64 と同じ構造で満たす。
- H5 の契約: arm64 の `cntpct_el0`（`bsp-rpi4/clock.c`）は `wfi` の間も進む。sparcv9・m68k・i386 の PIT 系の counter は `hal_cpu_idle()`（nop・hlt）の間も
  進む。どれも `hal_cpu_idle_suspend()` に入らないので新しい約束は増えない。

## 9. 敵対的レビュー（自分で）

| 懸念 | 判断 |
| --- | --- |
| EC の query（battery など）で頻繁に起きて SLP_S0 に留まれない | §7-4 の spurious の再突入で対処（Linux と同じ構造）。EC が query を出す頻度は 5330 の UAT で見る。上限を超えたら理由 = spurious で起きる（永久の loop にしない） |
| wake の IRQ の handler が走る規則を将来の driver が破る（D3 の device の MMIO を読む） | 今の wake の源は SCI だけ。規則を p006 の文書と `kern_irq_set_wake()` の comment に書き、driver の review の項目にする |
| `hal_irq_suspend()` 中に MSI が来て捨てられる（device が suspend の順の途中で送る） | device の suspend（段 3）は `hal_irq_suspend()`（段 5）より前に終わり、driver は割り込みを mask している（p004 の NVMe・xHCI・HDA はそうしている）。捨てるのは契約に書いた |
| LINT1 の NMI・machine check が sleep 中に来る | machine check は mask できず今と同じ。HAL の panic の NMI は ICR の IPI なので LVT に依らず届く（`amd64_lapic_panic_all`）。LINT0/LINT1 の LVT は delivery mode が NMI か ExtINT の物には触らない（pin の NMI は mask しない。ExtINT は PIC が masked なので来ない）。zedBSD は pin の NMI を使っていない |
| TSC が S0ix で止まる machine | §4-5 の RTC との照合の sanity check を p006 に。5330 で食い違えば、RTC の秒で補正する fallback を判断 |
| CPU 0 以外が先に起きて `kernel_ticks` が古いまま動く | §4-2。affinity を CPU 0 に向け、T の notify で CPU 0 が直ちに戻る。timeout が長くなる方向で短くならない |
| T の CPU が idle に入れない（T 以外の kernel の thread が走り続ける） | 走り続ける thread があれば入れないのは Linux も同じ（残る thread は普通は無い）。p006 で `KERN_SYSTEM_SLEEP_INFO` に「入れなかった時間」を出すか検討 |
| `hal_cpu_idle_suspend()` の LVT の保存・復元で LAPIC の error LVT を mask するのは診断を失う | sleep の間だけ。`amd64_error_interrupt()` は HAL_FATAL なので sleep 中に出る方が困る。契約には register の名前を出さず「private な源」とだけ書いた |
| `hal_irq_set_wake` を `hal_irq_suspend` 中に呼べない（STATE） | kernel は段 5 の前に arm する。boot で 1 度（SCI）か driver の attach で呼ぶ |
| probe が BSP の事実だけで答える時期（`hal_cpu_start_others()` 前） | kernel は boot の後にしか sleep しないので問題にならない。契約は「start_others の後は変わらない」 |
| 第 1 版の `*.diff` が同じ directory に残り混同する | README.md（第 1 版）の先頭に「第 2 版に置き換えた」の注記を足した（§10）。file は履歴として残す |

## 10. 記録の更新

- [design.md](../design.md) §6（HAL の差分の案を第 2 版に）、§9 の p006 の行（流れを §7 に合わせた）。
- [ws.md](../ws.md) の p002・p006 の行と resume point。
- [README.md](README.md)（第 1 版）の先頭に第 2 版への注記。

## 11. 確認

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| 差分が単独で当たる | worktree p4（777caf8d、hal.h は main c6f504c4 と同一）で `patch -p1 --dry-run < plan/ws052/proposed/<name>.diff` ×4 | 4 つとも `checking file include/hal/hal.h` で PASS（2026-10-05） |
| 4 つ続けて当たる | scratch の copy に H1v2 → H3v2 → H4v2 → H5 の順で `patch -p1`、結果が 4 つを一度に当てた header と `cmp` で一致 | PASS（H4v2 は offset 48 行、H5 は offset 106 行で当たる） |
| build・QEMU | — | 未実施（code を書いていない。hal.h も変えていない） |

## 12. 残る判断（ユーザー・Q1）

1. H1v2・H3v2・H4v2・H5 の承認（差分ごと。file 名と SHA256 は §1）。
2. H1v2 に含む HAL の責務の追加: amd64 の HAL が ACPI の LPIT を読む（MADT・MCFG と同じ経路）。
3. `hal_irq_resume()` を int にした（専門家は void）。void がよければそれに従う。
4. p006 の計画に無い依存の候補: user の process の停止（freeze）の機構が kernel に無い。p006 の範囲に入れるか別の Phase にするか（Q1）。
5. S0ix の間の TSC（§4-5）: 5330 の UAT で RTC と照合する。食い違えば fallback の判断。
