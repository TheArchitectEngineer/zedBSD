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

- address は下位 32 bit（kernel は ffff0000_xxxxxxxx）。関数名は `build/llvm/bin/llvm-addr2line -f -e kernel.elf 0xffff0000<8桁>` か `llvm-objdump -d --start-address=…`（kernel.elf は DWARF 無し、symbol は有り）。
- `irq-off ... at X < Y`: X は IRQ を止めたコード（`spin_lock_irqsave` など、LTO で inline された先）、Y はその呼び出し元（lock を取った所）。
- `in-irq N`: GIC の INTID（30 = timer、0/1 = SGI、32 以上 = device）。
- `STOPPED`: tick が 2 秒以上来ていない CPU。`chain`: その CPU が IRQ を止めて spin している時に自分で記録した return address の列。
- `unhandled exception ... esr=f2000001`: `brk #1`（`__builtin_trap()`）。elr が trap の場所。

## 2. 固まりの候補（source を読んで、2026-10-13）

| # | 所 | 何が起きるか | 4 core だけか | 確度 | 扱い |
| --- | --- | --- | --- | --- | --- |
| 1 | `src/kern/lock.c` `spin_trylock` | 取られている lock の `owner_valid` と `owner_cpu` を plain の load で読む。arm64 は 2 つの load を入れ替えてよいので、直前に lock を持っていた CPU C が、次の持ち主 D の `owner_valid=1` と自分の古い `owner_cpu==C` を組で読み、`__builtin_trap()`（偽の「再帰の取得」）→ `brk` → その CPU が IRQ を止めて止まる（CPU 0 なら device の IRQ が全部止まり ping も止まる。持っていた lock は残り他も固まる）。x86 は load の入れ替えが無いので amd64 では出ない | はい（arm64 の弱い順序） | 高 | 直した（Q1 許可）: `owner_valid` を acquire で読む |
| 2 | `src/drivers/gpu/bcm2711/render-worker.c` | `waitq_wake_one/all` を `hardware.guard` を外した後に呼ぶ所が 12 箇所。waitq の list の操作は condition lock を持つ前提。SMP では起こしの取りこぼし（executor・decoder が永久に眠り GPU が止まる）と list の破壊（`EBUSY` → `__builtin_trap()`、または IRQ を止めたまま輪を歩く） | はい | 高 | 直した（Q1 許可、D5 の例外）: guard の中で起こす |
| 3 | 以下は次の節 | | | | |

## 3. 結果

- 実装（commit は下の表）と build・host 試験は「確かめ」の節。
- Pi の実機: 未実施（Q1 が vmunix を置いてユーザーに再現を頼む）。

## 確かめ

（実装の後に追記）
