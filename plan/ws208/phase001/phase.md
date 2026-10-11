<!-- awesome-plan project=zedbsd record=ws208-p001 -->

# ws208-p001: arm64 の HAL の SMP（調査と設計）

Phase ID: `ws208-p001`
Parent: [WS208](../ws.md)
Status: in-progress（2026-10-11 P2、設計の第 1 版。判断の点 D1〜D6 は Q1 経由でユーザーへ）
Phase disposition: normal
Queue: q939（P2、2026-10-11）
依存: なし（調べは main 384908e71 の source）
所有 path（実装の Phase）: `src/hal/arm64/`（bsp-rpi4 を含む）、`platform/arm64/vmunix.mk` の HAL の source の行、`tests/rpi4-fdt-host-test.c`、`plan/ws208/`。hal.h は変えない（承認不要の範囲）。

## 1. 今（調べ、2026-10-11）

kernel の MI の部分は SMP に対応済みで amd64 で動いている。arm64 の HAL は UP だけ。

- MI: `src/kern/entry.c` が `thread_prepare_secondaries(hal_cpu_count())` → `hal_cpu_start_others()` → `sched_wait_others_online()` → `kern_cpu_notify_probe()`（全 secondary に `hal_cpu_notify_mask` が届くかを数える）→ `boot: CPUs ready: N`。secondary は HAL から `hal_task_create_for_init_context()` の後に `kernel_secondary_entry(cpu)`。`kernel_timer_handler(cpu, ack)` は CPU ごとに呼ばれ、共有の tick は CPU 0 だけが進める。`sched.c` は `hal_task_transfer(task, cpu)` で task を別の CPU へ渡す。
- 参考の実装: `src/hal/amd64/smp.c`（592 行）・`percpu.{c,h}`・`task.c` の `switching_from` の手順（切り替え元の task は、切り替え先の文脈が自分の stack を離れるまで `run_cpu` を立てたまま。`amd64_task_finish_switch()` が外す）。
- arm64 の UP の所（直す所の一覧）:
  | 所 | 今 | SMP で要ること |
  | --- | --- | --- |
  | `locore.S` | MPIDR の Aff0≠0 は `wfe` で park。BSS の clear、boot の page table、MMU の有効化は boot CPU だけ | secondary の入口（EL2→EL1、CPU ごとの stack、最終の page table で MMU、高位へ、C へ） |
  | `src/hal/cpu-up.c` | count=1、current=0、notify は UNSUPPORTED | arm64 は `src/hal/arm64/smp.c` に置き換え（vmunix.mk の `ARM64_BOOT_C` から cpu-up.c を外す） |
  | `task.c` | `running_task` は全体で 1 つ、`run_cpu` は 0 固定、`hal_task_transfer` は cpu≠0 を拒む、`task_list` に lock 無し | CPU ごとの running task、`switching_from` の手順（kernel・user の新しい task の入口と `asm_task_dispatch` の後で外す）、`target_cpu`、task の一覧の lock |
  | `irq.c` | `active_ack` は全体で 1 つ。affinity は CPU 0 だけ | CPU ごとの `active_ack`。affinity は D1 |
  | `bsp-rpi4/gic.c` | distributor と CPU interface を 1 回だけ初期化 | CPU ごとに banked の GICD（IGROUPR0・ISENABLER0・IPRIORITYR0〜7・SGI/PPI）と GICC の初期化、SGI の送り（GICD_SGIR） |
  | `bsp-rpi4/clock.c` | `timer_deadline` などが全体で 1 つ、`kernel_timer_handler(0, …)` 固定 | CPU ごとの CNTP（PPI 30）の deadline、`kernel_timer_handler(cpu, …)` |
  | `space.c` | `hal_cpu_count()!=1` で FATAL。`current_space` は全体で 1 つ。space の lock は「irq を止めて flag」で、同時に取られると FATAL（recursive）。registry・id・数に lock 無し。`flush_locked` は今の CPU の space だけ flush | CPU ごとの current space、本当の spin lock、registry の lock、page table の変更は常に broadcast（D2） |
  | `page.c` | 物理 page の allocator に lock 無し | pmem の spin lock（amd64 の `pmem_lock` と同じ形） |
  | `debug.c` | `debug_points_loaded` は全体で 1 つ、`arm64_debug_init` は boot CPU だけ | CPU ごとに。secondary でも OS lock の解除と MDSCR の初期化 |
  | `asm.c` | `arm64_flush_tlb()` は `tlbi vmalle1is`（broadcast）。`hal_reset`・`hal_panic` は自分の CPU だけ | D2。panic は他の CPU を止める（`hal_cpu_panic_all`） |
- 既に SMP に向いている所: TCR の SH0・SH1 は inner shareable、page の descriptor の SH も inner shareable（0x701、leaf_flags）、cache の保守は `dsb ish`・`ic ivau`（PoU の IS の broadcast）、`hal_mb` は `dmb ish`。kernel の atomic は `-march=armv8-a -mno-outline-atomics` の LL/SC（MMU 有効・Normal の inner shareable で動く）。
- 起動の方式: Pi 4 の firmware（armstub8）と QEMU raspi4b の `-kernel`（arm64 の Linux の header を持つ image として boot）は共に spin-table。secondary は firmware の stub の中で `wfe` しながら `cpu-release-addr`（Pi 4 の DT では 0xd8・0xe0・0xe8・0xf0）を見ており、そこへ入口の物理 address を書いて `sev` すると飛んでくる。入るのは EL2（非 secure）か QEMU の場合の EL1。低位 1 MiB は `arm64_page_init` が予約済みなので spin-table は壊れない。PSCI は Pi 4 の既定の firmware には無い。
- Pi 4 の driver: `rpi4-sdhci.c` は mutex（SMP で可）。GENET・xHCI・PCIe・firmware の mailbox・console と、WS141 の bcm2711 の GPU（`src/drivers/gpu/bcm2711/`）は p004 で競合を確かめる。今の割り込みは全て CPU 0 に届く（ITARGETSR=1）。

## 2. 設計

### 2.1 CPU ごとの領域

- `struct arm64_percpu`（`src/hal/arm64/percpu.h`）: `self`、`logical_id`、`mpidr`、`ready`、boot の stack（物理 address と大きさ）、`running_task`、`switching_from`、`current_space`、`active_ack`、timer の `deadline` と `ticks`、debug の `points_loaded`。CPU 0〜3 の静的な配列（`ARM64_CPU_MAX 4`、Pi 4 の core の数。DT の cpu の数が上回れば 4 で切って log）。
- 自分の領域は `TPIDR_EL1` に置く（今は未使用。Linux も per-cpu の offset に使う）。`hal_cpu_current()` は `TPIDR_EL1` の `logical_id`。MPIDR の Aff0 と logical id の対応は DT の cpu の `reg` から。
- 例外の入口（`trap.S`）は TPIDR_EL1 を触らない。user の TLS は今どおり `TPIDR_EL0`。

### 2.2 secondary の起動

1. `rpi4_fdt_parse` に `/cpus/cpu@N` の `reg`・`enable-method`・`cpu-release-addr` を足す（`struct rpi4_fdt_info` に cpu の表）。`tests/rpi4-fdt-host-test.c` に host 試験を足す（Pi 4 の DT の形の blob）。
2. `hal_cpu_start_others()`（`src/hal/arm64/smp.c`）: CPU ごとに boot の stack（16 KiB、`hal_pmem_alloc`）を取り、`arm64_secondary_boot[cpu]`（stack の top、`TPIDR_EL1` に入れる percpu の address）を書いて cache を clean、spin-table の `cpu-release-addr` に入口 `arm64_secondary_entry` の物理 address を書き、`dc civac` と `dsb sy` の後に `sev`。`ready` を最大 1 秒待つ。`hal_cpu_count()` は start の前に決まる数（DT の cpu の数、4、D3 の `cpus=N` の小さい方）で、MI の `thread_prepare_secondaries` はその数の CPU が online になる前提なので、来なかった CPU の扱いは D6。
3. `arm64_secondary_entry`（`locore.S`）: `daifset`、EL2 なら boot CPU と同じ EL2 の設定（HCR・CPTR・CNTHCTL・CNTVOFF・SCTLR_EL1）で EL1 へ。CPACR・CNTKCTL、SP は `arm64_secondary_boot` から物理で、MAIR・TCR は boot と同じ値、TTBR1 は `system_kernel_l0`（`arm64_space_init` の後の最終の表）、TTBR0 は `system_ttbr0`、`tlbi vmalle1`・`ic iallu`、SCTLR で MMU と cache、高位の address へ飛ぶ。`arm64_secondary_main(cpu)` を呼ぶ。最終の表の物理 address は boot CPU が `arm64_secondary_boot` に書いておく。
4. `arm64_secondary_main(cpu)`（C）: `TPIDR_EL1`、`VBAR_EL1`、GIC の CPU ごとの初期化、debug の初期化、`hal_task_create_for_init_context()`、CPU ごとの timer の開始、`ready`＝1 と ready mask、`kernel_secondary_entry(cpu)`。

### 2.3 割り込みと CPU 間の通知

- GIC-400 の CPU ごとの初期化: banked の IGROUPR0（今の boot CPU と同じ「secure なら Group 0、非 secure なら書いても無視される」の扱い）、SGI 0・1 と PPI 30 の priority、PPI 30 の enable、GICC_PMR=0xff、GICC_CTLR。
- `hal_cpu_notify(cpu)`・`_mask`: SGI 1 を GICD_SGIR（target list、`NSATT` は boot CPU の group の判定に合わせる）で送る。受けた CPU は `kernel_cpu_notify_handler(cpu, ack)`。SGI 0 は HAL の内部（panic の停止）に取っておく。
- `hal_cpu_park`・`hal_cpu_panic_all`: SGI 0 で他の CPU を止める（IRQ を止めている CPU には届かない。amd64 の NMI に当たる物が GIC-400 の非 secure には無い。FIQ は firmware の設定次第で使えない → 制限として記録）。
- 外の割り込み（SPI）の宛先: D1。
- CPU ごとの generic timer: 各 CPU が自分の CNTP_CVAL・CNTP_CTL を設定し、PPI 30 を自分の CPU interface で受ける。`kernel_timer_handler(cpu, ack)`。

### 2.4 task と切り替え

- amd64 と同じ手順: `hal_task_context_switch` は `to->run_cpu` が −1 で `to->target_cpu` が自分であることを確かめ、`switching_from = from`、`to->run_cpu = 自分`、percpu の `running_task = to`、space・FPU・TLS・debug を切り替えて `asm_task_dispatch`。戻った所と `arm64_kernel_task_entry`・`arm64_user_task_entry`・`arm64_user_frame_entry` の最初で `arm64_task_finish_switch()` が `switching_from->run_cpu = −1`（release）。
- `hal_task_transfer(task, cpu)`: `run_cpu` が −1 で cpu が ready なら `target_cpu = cpu`。
- `task_list` と数は spin lock。

### 2.5 address space と TLB

- `current_space` を CPU ごとに。`hal_space_destroy` の「使っている CPU が無い」の確かめは全 CPU の percpu を見る。
- space の lock を本当の spin lock（irq を止めて `ldaxr`/`stlxr` の try で回る）に。同じ CPU での入れ子は今どおり FATAL。registry・id・数は global の spin lock。
- TLB（D2 の推し）: space の切り替えでは自分の CPU だけを `tlbi vmalle1`（broadcast しない）。page table の変更（map・unmap・prot・clear_flags・device の map）と kernel の写像の変更は常に broadcast（`tlbi vmalle1is`、範囲が小さい時は `tlbi vae1is`/`vaae1is` を後の最適化に）＋`dsb ish`＋`isb`。ASID は使わない（全ての user の entry は nG だが、切り替えで自分の CPU の TLB を空にするので、自分の TLB に残るのは今の space の entry だけ）。
- I-cache の同期（`hal_sync_instruction_stream`）は `ic ivau`＋`dsb ish` が IS の broadcast なので今のまま。

### 2.6 その他

- `page.c`: amd64 と同じ形の `pmem_lock`（irq を止めて spin）。
- `irq.c`: `active_ack` を percpu に。`hal_irq_set_handler` は slot の更新を lock で守り、`hal_irq_unregister` は他の CPU で走っている handler の終わりを待つ契約（hal.h）に合わせて、slot ごとの「走っている数」を見て待つ。
- `debug.c`: `debug_points_loaded` を percpu に。`arm64_debug_init` を secondary でも。
- `hal_reset`: 他の CPU を止めてから watchdog（今は自分だけ IRQ を止める。watchdog の full reset は全 core を戻すので必須ではないが、止めてから reset する）。
- 起動の log: `ARM64 SMP cpu N online (mpidr …)`。

### 2.7 規約

触る関数は coding-style の全文で書き直す（arm64 の HAL は圧縮の書き方が多い。D4）。新しい file（`smp.c`・`percpu.{c,h}`）は全文。

## 3. Phase の分け方（案）

| Phase | 内容 | 確かめ | 見積もり（LW） |
| --- | --- | --- | --- |
| p002 | 下地: percpu と TPIDR_EL1、`hal_cpu_current`、pmem・space・task・irq の lock、task の `switching_from`・`target_cpu`、CPU ごとの current space・active_ack・timer・debug。まだ 1 CPU で動かす（cpu-up.c のまま、count=1） | build・UP の起動（T1、QEMU raspi4b、boot-test の PNG） | 4〜5 |
| p003 | secondary の起動: FDT の cpu の表（host 試験）、`locore.S` の secondary の入口、`smp.c`（start_others・notify・park・panic_all・ready mask）、GIC と timer の CPU ごと、TLB の切り替えの local と変更の broadcast、`cpus=N` の boot parameter（D3） | build、host 試験、T1 で QEMU raspi4b `-smp 4` の起動と `CPUs ready: 4` の PNG | 4〜6 |
| p004 | 負荷と driver の競合: QEMU の `-smp 4` で build・desktop・network の負荷、GENET・xHCI・PCIe・mailbox・console の競合の直し（GPU は D5） | T1（QEMU）、短い負荷 | 2〜4 |
| p005 | Pi 4 の実機: 4 core の起動、desktop・network・SSH、reboot 10 回（Q1 が P3 と順を合わせる） | 実機 | 4〜6 |
| p006 | 規約の全文の見直し | build・diff の確かめ | 1 |

合計 15〜22 LW（Q1 の見積もり 16〜22 と同じ程度）。

## 4. 判断の点（推し付き、Q1 経由でユーザーへ）

- **D1 外の割り込みの宛先**: (a 推し) WS208 では全ての SPI を CPU 0 に（今のまま。`hal_irq_set_affinity` は CPU 0 を含む mask を受け、effective は CPU 0）。driver の handler が同時に 2 つの CPU で走らないので競合が減る。分散は Future Work。(b) `hal_irq_set_affinity` の通りに ITARGETSR を設定。
- **D2 TLB と ASID**: (a 推し) ASID を使わず、切り替えは自分の CPU の flush、変更は broadcast。正しさが単純。(b) 8/16 bit の ASID と世代の巻き戻し（切り替えの flush が要らない、+2〜3 LW）。ASID は性能が問題になった時の Future Work に。
- **D3 CPU の数の boot parameter**: (a 推し) cmdline.txt の `cpus=N`（既定は DT の全部）。実機で SMP が不安定な時に `cpus=1` で今と同じ UP に戻せる。(b) 無し。
- **D4 規約**: (a 推し) 触る関数だけ全文で書き直し、触らない関数は今のまま（p006 で WS の差分を見直す）。(b) 触る file を全部書き直す（+2 LW）。
- **D5 WS141 の GPU の driver の競合**: SMP で `src/drivers/gpu/bcm2711/` に競合が出たら誰が直すか。(a 推し) P2 が見つけて log・再現の手順を Q1 に出し、直しは WS141 の別 session（その driver の文脈を持つ）。(b) P2 が直して WS141 の session に patch を渡す。
- **D6 secondary が来なかった時**: (a 推し) FATAL で止める（MI は count 個が online の前提）。画面に `cpus=1` で起動できると出す。(b) 来た CPU の数で続ける（MI の `thread_prepare_secondaries` の後に数を変えることになり、MI の変更が要る）。

## 5. 危険と未確定

- 非 secure の EL1（firmware）と secure の EL1（QEMU の `-kernel`）で GIC の group が違う（今の boot CPU の `gic.c` の注記）。SGI の group と `NSATT` は両方で確かめる（p003 で QEMU、p005 で実機）。
- firmware の armstub8 が secondary を EL2 で渡すか（Pi 4 の既定の armstub は EL2）。両方の入口を作る。
- secondary の入口では最終の kernel の page table（`system_kernel_l0`）を直接使う。locore の boot の表（1 GiB の identity）は secondary には使わない（MMU を有効にする瞬間の PC は物理 address なので、TTBR0 に identity の表が要る → boot の表 `arm64_l0_low` を残して TTBR0 に一時的に入れ、高位に飛んでから `system_ttbr0` に替える）。
- 今の UP の kernel の driver に「irq を止めれば排他」の前提が残っていないか（MI は amd64 で SMP 済み。Pi 4 だけの driver が対象）。

## 6. 結果

- 2026-10-11 P2: source の調べと設計の第 1 版（この文書）。判断の点 D1〜D6 を Q1 へ。実装は未着手。
