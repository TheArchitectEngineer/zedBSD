<!-- awesome-plan project=zedbsd record=ws208-p007 -->

# ws208-p007: Pi の 4 core の固まり（検出の仕組みと候補）

Phase ID: `ws208-p007`
Parent: [WS208](../ws.md)
Status: in-progress（2026-10-13 P6: stall watch と 2 つの直しを実装、Pi の実機の再現待ち）
Phase disposition: normal
Queue: Q1 の依頼（2026-10-13、P6）
依存: [p004](../phase004/phase.md) の main の統合（main a14f7d5bd の image で起きた）

## 起きた事（2026-10-13、Q1 から）

Pi 4、main a14f7d5 の image、4 core の既定。窓を 3 つ出して arrange（compositor の並べ替えのアニメ）を繰り返すと、起動から約 2 分で system 全体が固まる（SSH も ping も通らない）。Q1 の watch.sh の最後の記録（uptime 00:02）では kernel の log に異常は無く、何もしていない時も top の sy が 51〜56%（F-097）。同じ image で `cpus=1` にすると 10 回試しても固まらない。同じ MI の kernel の amd64 の SMP では起きない。serial の console は無効、画面は display の driver が持つので、固まった後に読める物が無かった。

## 1. 検出の仕組み（stall watch、`src/hal/arm64/watch.{c,h}`）

既定で有効。費用は、IRQ を止める時（on→off の変化だけ）の counter の読みと 3 つの store、IRQ の入口の 4 つの store、`hal_cpu_current()` の減算 1 つ、timer の割り込みの store 1 つと 128 tick ごとの 4 CPU の読み、`hal_putc()` の 1 文字ごとの atomic の加算。報告は 1 秒に 1 回まで、止まった時だけ。

| 何を | どこで |
| --- | --- |
| CPU ごとの heartbeat（tick の時刻） | `rpi4_timer_interrupt()` → `arm64_watch_beat()` |
| 他の CPU の heartbeat の確かめ（128 ms ごと、2 秒以上古い CPU を「STOPPED」） | 生きている全 CPU の tick（CPU 0 が止まっても他が見る） |
| IRQ を止めた時刻と場所（コードの PC と呼び出し元） | `hal_irq_disable()`（on→off の時だけ）、`hal_irq_enable()` で消す、IRQ の入口でも消す |
| 処理中の IRQ の番号・開始時刻・割り込まれた PC | `arm64_irq_handler()` |
| task の切り替えの数（scheduler が進んでいるか） | `hal_task_context_switch()` |
| 自分の tick が 2 秒以上来ないのに走っている CPU（IRQ を止めて spin）→ 自分の call chain（frame pointer で 6 段） | `hal_cpu_current()` の 4096 回ごと（kernel の spin lock は毎回呼ぶ）、HAL の spin lock・console の lock の 65536 回ごと |
| HAL_FATAL・hal_assert・未処理の同期例外（esr・elr・far） | `hal_fatal()`・`hal_assert()`・`arm64_sync_handler()` |
| kernel の log の末尾（4 KiB） | `hal_putc()`（rpi4 では klog の全 record が `kern_platform_debug_write()` → `hal_putc()` を通る。panic の文も） |

出し先:
- (a) 画面: display の driver が画面を持つ時（`rpi4_cons_output_suspended()`）は HVS（0xfe400000）の DISPLIST0（+0x20）が指す display list を歩き、unscaled・RGBA8888・8 word・128×128 以上の plane の全部に、画面の (16,16) から 120 桁×40 行の文字を描いて PoC まで clean する（白の文字、背景は byte の順で赤か青）。止まりが 10 秒続いたら、display list を「最大の plane 1 つだけ」の list（SRAM の word 1984、driver の使う 0〜1023 の外）に替え、上の窓が文字を隠さないようにする（driver が生きていれば次の flip で戻る）。driver が画面を持っていない時（boot 中・QEMU）は firmware の framebuffer の 80×25 の cell に白地青で描く。
- (b) memory: `arm64_watch_text`（40 行×121 byte）。gdb なら `x/40s arm64_watch_text`。
- kernel の log の ring には書けない: HAL から klog に書く API が hal.h に無い（足すなら hal.h の差分で承認が要る。今回は足していない）。代わりに画面に kernel の log の末尾を出す。

取れない場合: 全 CPU が、`hal_cpu_current()` も HAL の spin も呼ばない loop で IRQ を止めて止まった時（例: kernel の heap の lock の spin は `hal_compiler_barrier()` だけ）。GIC-400 の FIQ は firmware の下の非 secure の kernel では Group 0 を使えず、BCM2711 の watchdog（PM_WDOG）は board を reset するだけで画面が消える。Cortex-A72 の外部 debug の PC の sample（EDPCSR）は memory-mapped の debug の address が BCM2711 で未確認。

### 画面の読み方

```
zedBSD stall watch: cpu0 has not taken its tick (seen by cpu2)
uptime 123.456s  report 3  stop for 2.010s  addresses: ffff0000 + shown
cpu0 unhandled exception esr=f2000001 elr=ffff0000000d035c far=...   ← 最初の fatal（あれば）
cpu0 STOPPED tick -2.345s irq-off -2.345s at 000d043c < 000a1234
     in-irq 33 -2.300s from 000b5678 switches 12345 ticks 120000 task 0000xxxxxxxx
     chain -2.100s: 000d0360 000d0468 000a1234 ...                       ← 自分で spin を見つけた CPU だけ
cpu1 tick -0.001s irq-off no
     in-irq no switches ... ticks ... task ...
...
kernel log (last lines):
<klog の最後の 12 行>
```

- address は下位 32 bit（kernel は ffff0000_xxxxxxxx）。関数名は `build/llvm/bin/llvm-objdump -d --no-show-raw-insn --start-address=0xffff0000<8桁> --stop-address=0xffff0000<8桁+4> <その vmunix と組の kernel.elf>` の `<関数名>:` の行（kernel.elf は DWARF 無し、symbol は有り。vmunix と同じ build の kernel.elf を使う）。
- `irq-off ... at X < Y`: X は IRQ を止めたコード（`spin_lock_irqsave` など、LTO で inline された先）、Y はその呼び出し元（lock を取った所）。
- `in-irq N`: GIC の INTID（30 = timer、0/1 = SGI、32 以上 = device）。
- `STOPPED`: tick が 2 秒以上来ていない CPU（up で、最初の tick を取り、park していない CPU だけ）。`parked`: panic の後・power-off で意図して止めた CPU（判定しない）。`chain`: その CPU が IRQ を止めて spin している時に自分で記録した return address の列。
- `unhandled exception ... esr=f2000001`: `brk #1`（`__builtin_trap()`）。elr が trap の場所。

## 2. 固まりの候補（source を読んで、2026-10-13）

手掛かり: `cpus=1` では起きない、amd64 の SMP（同じ MI）では起きない、特定の描画（arrange）で起きる、ping も止まる（device の割り込みは全部 CPU 0、D1）。→ arm64 の弱い memory の順序で初めて出る MI の誤り、または bcm2711 の driver の SMP の競合を第一に疑った。bcm2711 の driver は Explore の subagent に読ませ、要所を自分で確かめた。

| # | 所 | 何が起きるか | 4 core だけか | 確度 | 扱い |
| --- | --- | --- | --- | --- | --- |
| 1 | `src/kern/lock.c` `spin_trylock` | 取られている lock の `owner_valid` と `owner_cpu` を plain の load で読む（kernel.elf: `ldar held; ldr owner_valid; ldr owner_cpu; cmp; brk #1`）。arm64 は 2 つの load の順を入れ替えてよいので、直前に lock を持っていた CPU C が、次の持ち主 D の `owner_valid=1` と自分の古い `owner_cpu==C` を組で読むと `__builtin_trap()`（偽の「再帰の取得」）→ kernel の `brk` → その CPU は IRQ を止めて止まる。CPU 0 なら device の割り込みが全部止まり ping も止まる。持っていた lock は残るので他の CPU も順に固まる。2 つの CPU の間の lock の取り合い（arrange の描画で多い）ほど窓が開く。x86 は load の入れ替えが無いので amd64 では出ない | はい（arm64 の弱い順序） | 高 | **直した**（Q1 許可）: `owner_valid` を acquire で読む。`klog.c` の fatal の経路の同じ形も |
| 2 | `src/drivers/gpu/bcm2711/render-worker.c`（WS141 p016 の executor） | `waitq_wake_one/all` を `hardware.guard` を外した後に呼ぶ所が 12 箇所（submit・stop・drain・fault・commit・cancel・finish・execution の submit・executor の完了・window の close・map の unlock）。waitq の list の操作と起こしの判定は condition lock を持つ前提（`waitq_sleep` の登録と `sched_sleep_locked` の間に、lock の無い起こしが token を外し、`sched_wakeup` は RUNNING の thread を無視する）。SMP では起こしの取りこぼし（executor・decoder・mapper が永久に眠り、GPU が止まり、`pte_waiters` が残って window の open も止まる）と list の破壊（`EBUSY` → 460・1052・1519・1653・1732 の `__builtin_trap()`、または輪を IRQ を止めて歩き、CPU 0 の `service_sources` が guard で spin） | はい（1 CPU では起こす側が sleeper の IRQ を止めた窓に入れない） | 高 | **直した**（Q1 許可、D5 の例外）: 起こしを guard の中へ（display-flip.c・v3d-hardware.c と同じ形）。map_unlock は mutex を返した後に guard を取って起こす |
| 3 | `display-flip.c` `flip_wait`・`wait_console` | vblank の割り込みが `FLIP_ADOPTION_TICKS` 内に来ないと `flip.uncertain` になり、以後の record が EBUSY（console を戻すまで）。CPU 0 が長く IRQ を止める（`hal_pmem_map_uncached`・`unmap_uncached` の大きな buffer の `dc civac` を `kernel_table_lock` の下で）と起きうる | いいえ | 低 | 画面だけ止まる形（ping は通る）。今回の症状とは違う。記録だけ |
| 4 | `hal_dcache_clean_range` などの終わりの `dsb ish` | 非 coherent な DMA（HVS・mailbox・V3D）の前は Linux は `dsb sy`。実際は MMIO の barrier が順を作る | いいえ | 低 | 記録だけ（壊れるなら絵、固まりではない） |
| 5 | `rpi4-firmware.c` の timeout の後 | 応答の遅れが次の要求の応答に見える（EIO） | いいえ | 低 | 記録だけ |
| 6 | `src/hal/arm64/space.c` `space_lock_handle` | registry の lock を持ったまま space の lock を IRQ を止めて spin する。space の lock の中で registry を取る経路は無い（heap と pmem だけ）ので deadlock ではないが、長い space の操作の間は全 CPU の space の操作が止まる | 性能 | 低 | 記録だけ（Future Work 候補） |
| 7 | F-097（何もしていない時の context switch の空回り、sy 51〜56%） | 4 core で sched の lock を取り合い、上の #1 の窓を広げる | 増やす | 中（#1 の引き金として） | 記録だけ（F-097 のまま） |

確かめて SMP に問題が無かった所（arm64 の HAL）: kernel の atomic（`__atomic` の acquire・release、LL/SC）、HAL の spin lock（`ldaxr`/`stxr` と `stlr`）、task の切り替えの `run_cpu`（release・acquire、`hal_task_transfer` が走っている task を拒む）、timer と CPU 間の通知の handler は EOI を先に出してから scheduler（切り替えで EOI が残らない）、SGI の送りの前の `dsb ish`、page table の変更の `tlbi vmalle1is`＋`dsb ish`。bcm2711: 全ての guard は irqsave、IRQ と共有の lock に plain の `spin_lock` は無い、display の起こしは全部 guard の中、lock の順の逆転は見つからない。

## 3. その他（Q1 の追加の依頼）

- QEMU raspi4b の audio の fault（T1-553、`audio_start+0xe4`、far=0xffff0000fe20c800 = PWM1）: QEMU は PWM1 を emulate しない。`rpi4-audio.c` で、device tree の PCIe の controller が disabled（QEMU の raspi4b の印、`rpi4-pcie.c` も同じ印を使う。実機の firmware は付けない）なら jack を attach しない（`rpi4-audio: emulated board without PWM1; no headphone jack`）。
- QEMU の「ignored 1 unknown parameter; first=cpus」: MI の kernel の boot parameter の表（`src/kern/boot.c`）に `cpus` が無いので出る表示だけ。HAL の `cpus=N` は `/chosen/bootargs` を読み、QEMU でも T1-537 (d) で `CPUs ready: 1` になっている。消すなら boot.c の表に `cpus` を足す（MI、WS208 の外。今回はしていない）。

## 4. 偽の停止の報告（T1-555、2026-10-13 P6）

- 症状: QEMU raspi4b TCG 4 core で、起動の直後・reboot の後に `cpu1 has not taken its tick`、`cpu1 STOPPED tick -295147905179.352s`（reboot の後は cpu1〜3 と cpu2 の in-irq の時刻も同じ値）。guest は動き続ける。
- 原因: 値は 2^64 / 62.5 MHz（QEMU の CNTFRQ）= 295147905179.35 秒、つまり `now - beat` の符号なしの差が負に回った物。見る側の CPU は tick の始め（`rpi4_timer_interrupt()` の counter）の `now` で他の CPU の heartbeat を読むが、その間に他の CPU が tick を取ると heartbeat が `now` より新しくなる。QEMU は全 CPU の timer の期限がほぼ揃うので起きやすい（実機でも窓はある）。閾値の誤り・単位の誤りではない（counter はどこも CNTPCT、周波数は CNTFRQ）。2 秒の閾値・TCG の遅さの証拠は無い（画面の値は全部この回り込み）。
- 直し（`src/hal/arm64/watch.c`）: `time_since(now, then)` は then が now 以後なら 0（新しい）。停止の判定は `cpu_is_stopped()` に一本化（up でない・最初の tick の前・park した CPU は判定しない、年齢が閾値以上だけ停止）。画面の年齢（tick・irq-off・in-irq・chain・stop for）、1 秒の間隔、10 秒の takeover の判定も `time_since()`。意図した停止: `hal_cpu_park()`（panic の後の stop の SGI・panic した CPU）、`hal_poweroff()`、`hal_panic()` が `arm64_watch_park()` で per-CPU の `watch_parked` を立てる。起動の初めの猶予は入れていない（原因が回り込みで、起動中の真の停止を隠すため）。
- 真の停止の経路（2 秒以上古い heartbeat、self check、fatal・fault の報告）は同じ。
- 試験: `plan/ws208/tests/watch-judge-host-test.sh`（`time_since`・`cpu_is_stopped` を watch.c から切り出す）PASS。guard を外すと FAIL（`beat after now is not a stop`）を確かめた。rpi4 の vmunix rc 0・warning 0・`arm64 vmunix check: PASS`。QEMU は T1 に依頼（未実施）。

## commit（agent/p2、全部 WIP）

| commit | 内容 |
| --- | --- |
| 59e0af6d7 | stall watch（`src/hal/arm64/watch.{c,h}` と HAL の hook、`vmunix.mk`）と、この phase.md・ws.md |
| a1a78e5c1 | 候補 #1 の直し（`src/kern/lock.c`・`src/kern/klog.c`） |
| 05e867921 | 候補 #2 の直し（`render-worker.c`） |
| c2bcd98e3 | QEMU の audio（`rpi4-audio.c`） |
| 5c13a029f | watch.c の書き方の整え（binary は同じ） |

## 確かめ（2026-10-13 P6）

- rpi4 の vmunix（`make -j8 -o $PWD/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix`）: watch だけ（59e0af6d7）と全部（5c13a029f）の両方 rc 0・warning 0・`arm64 vmunix check: PASS`。
- amd64 の vmunix（`ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix`、lock.c・klog.c を含む）rc 0・warning 0。
- host: `plan/ws213/tests/lock-host-test.sh` PASS、`plan/ws141/tests/vulkan-device-host-test.sh` PASS、`plan/ws141/tests/v3d-hardware-host-test.sh` PASS。stall watch は HAL の asm（TPIDR_EL1・CNTPCT）に依るので host 試験は無し（未実施）。kernel.elf の逆 asm で `spin_lock_irqsave` に IRQ を止めた場所の記録、`spin_trylock` に countdown が入ったことを確かめた。
- QEMU・Pi の実機: 未実施（Q1 が置いてユーザーに再現を頼む）。

### Pi に置く物

| 物 | vmunix | kernel.elf | cksum |
| --- | --- | --- | --- |
| 全部入り（watch＋#1＋#2＋audio） | `/home/awe/zedBSD-worktrees/p6/build/ws208-p007-all/vmunix` | 同じ directory の `kernel.elf` | 4106892042 8130560（SHA256 868988da…46fe） |
| watch だけ（切り分け用、59e0af6d7） | `/home/awe/zedBSD-worktrees/p6/build/ws208-p007-watch/vmunix` | 同じ directory の `kernel.elf` | 3397053199 8130560（SHA256 3cf75804…c9d0） |

見る点: 全部入りで 4 core の arrange を繰り返して固まらないか。固まったら画面の写真（上の読み方）。watch だけの image で固まれば、画面の `unhandled exception esr=f2000001 elr=…` の elr が `spin_trylock` なら #1、`render-worker` の関数なら #2。

## 残り

- Pi の実機の再現（全部入り・watch だけ）。
- stall watch の誤報の確かめ（boot 中の 2 秒以上の IRQ の停止があれば、起動中の画面に出る）。
- HAL から kernel の log に書く API（hal.h の差分、承認が要る）は提案だけ。
