<!-- awesome-plan project=zedbsd record=ws208-p002 -->

# ws208-p002: arm64 の HAL の SMP の下地（1 CPU のまま）

Phase ID: `ws208-p002`
Parent: [WS208](../ws.md)
Status: in-progress（実装済み・T1 の試験待ち。p003 と同じ依頼で確かめる）
Phase disposition: normal
Queue: q939（P2、2026-10-11）
依存: [p001](../phase001/phase.md) の設計と判断 D1〜D6（2026-10-11 ユーザー「全部承認です。」）

## 範囲

CPU ごとの状態と HAL の内部の lock。まだ boot CPU だけで動く（`hal_cpu_count()` は 1）。

## 実装（commit 6484ad96d、base main b67a6d7e1 を merge した agent/p2）

- 新しい file: `src/hal/arm64/percpu.{c,h}`（CPU ごとの record を `TPIDR_EL1` で引く: running task・switching_from・current space・active_ack・timer・debug の状態・ready）、`src/hal/arm64/spin.h`（HAL の内部の spin lock: irq を止めて `hal_atomic_uint_try_acquire`）、`src/hal/arm64/smp.c`（CPU の API。arm64 は `src/hal/cpu-up.c` をやめ、`platform/arm64/vmunix.mk` の `ARM64_BOOT_C` を smp.c・percpu.c に）。
- `task.c`: 規約の全文で書き直し（D4: 触る関数）。running task を CPU ごとに、amd64 と同じ `switching_from`・`target_cpu`・`hal_task_transfer` の手順、`arm64_task_finish_switch()` を `dispatch.S` の 3 つの入口（kernel・user の新しい task、fork の子）と切り替えの戻りで呼ぶ。task の一覧と数は lock。
- `space.c`: registry・space・kernel の表（device の block、uncached の窓）の spin lock、space の lock は本当の spin lock と同じ CPU の入れ子の検出（`lock_owner`）、current space を CPU ごとに、page table の変更の flush は「どこかの CPU が選んでいれば broadcast」、table の数は atomic。
- `page.c`: 物理 page の allocator を `pmem_lock` で。
- `irq.c`・`bsp-rpi4/clock.c`・`debug.c`: active_ack・tick の deadline・debug の状態を CPU ごとに。
- `cmain.c`: 最初に `arm64_smp_bootstrap()`。

## 確かめ

- rpi4 の vmunix の named build（`make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws208-rpi4 build/ws208-rpi4/vmunix`）rc 0・warning 0。
- QEMU・実機: 未実施（p003 と合わせて T1 に依頼。`cpus=1` の起動がこの Phase の 1 CPU の経路の確かめになる）。
