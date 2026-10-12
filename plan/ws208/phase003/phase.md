<!-- awesome-plan project=zedbsd record=ws208-p003 -->

# ws208-p003: secondary の起動（spin-table・GIC・timer・TLB・cpus=N）

Phase ID: `ws208-p003`
Parent: [WS208](../ws.md)
Status: in-progress（実装済み・T1 の試験待ち）
Phase disposition: normal
Queue: q939（P2、2026-10-11）
依存: [p002](../phase002/phase.md)

## 実装（commit 50959a401・ab709082b）

- `bsp-rpi4/fdt.{c,h}`: `/cpus/cpu@N` の `reg`（MPIDR の affinity）・`enable-method`（spin-table）・`cpu-release-addr` を `struct rpi4_fdt_info` の `cpus[]` に。`tests/rpi4-fdt-host-test.c`（vmunix.mk の `rpi4-fdt-host-test` の規則が指していたのに無かった file）で Pi 4 の DTB を host で読んで確かめる。
- `locore.S`: `arm64_secondary_entry`（物理の入口）。EL2 なら boot CPU と同じ hand-off、MMU を boot の identity の表（TTBR0）と kernel の最終の表（TTBR1、boot block から）で有効にして高位へ、`arm64_secondary_main(cpu)`。boot block は MPIDR の Aff0 で引く 32 byte。EL2 では boot CPU も secondary も `VPIDR_EL2`・`VMPIDR_EL2` に本物の値を入れる（EL1 の MPIDR の読みがそれを返すため）。
- `smp.c`: `arm64_smp_configure()`（DT の spin-table の core を 4 つまで、cmdline の `cpus=N` で上限、D3）、`hal_cpu_start_others()`（16 KiB の stack、boot block を PoC まで clean、`cpu-release-addr` に入口の物理 address を書いて clean・`dsb sy`・`sev`、1 秒待つ。来なければ `cpus=1` の案内を出して FATAL、D6）、`arm64_secondary_main()`（record・`arm64_space_select_system()`・vector・GIC の CPU ごと・debug・初期 task・tick・ready → `kernel_secondary_entry`）、`hal_cpu_notify`・`_mask`（SGI 1）、`hal_cpu_panic_all`（SGI 0 で他を止める。IRQ を止めている CPU には届かない）。
- `bsp-rpi4/gic.c`: `rpi4_gic_init_cpu()`（banked の SGI・PPI、timer と SGI 0・1 を Group 0、CPU interface）、`rpi4_gic_send_sgi()`（`dsb ish` の後に GICD_SGIR）、distributor の group・設定の読み書きを lock で。外の割り込み（SPI）は全て CPU 0 のまま（D1）。
- `irq.c`: SGI 1 → `kernel_cpu_notify_handler`、SGI 0 → park。slot の handler と引数を lock の下で対で読む。
- `asm.c`・`space.c`: space の切り替えは自分の CPU の TLB だけを flush（`arm64_flush_tlb_local`、D2: ASID 無し）、`arm64_space_init` の UP の FATAL を外した。

## 確かめ

- rpi4 の vmunix の named build rc 0・warning 0（`build/ws208-rpi4/vmunix`、ab709082b、cksum 3189702959 8101888）。
- host: `cc -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Isrc src/hal/arm64/bsp-rpi4/fdt.c tests/rpi4-fdt-host-test.c` → `rpi4-fdt-host-test: PASS (4 CPUs, gic ff841000/ff842000)`（main の checkout の `vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb`）。
- QEMU raspi4b（4 core 固定）: T1 に依頼（Q1 経由）。実機: p005。

## 危険（未確認）

- Cortex-A72 の CPUECTLR_EL1.SMPEN は firmware（armstub8）が立てる前提。QEMU では関係ない。実機で coherency が壊れたらここを疑う。
- 非 secure（firmware）では SGI・PPI の group は firmware の設定（Group 1）に任せる。実機の notify は p005 で確かめる。

## T1-537 の結果（2026-10-11 T1、agent/p2 ea8ecb4cf、QEMU raspi4b。判定は Q1）

- (a) 既定で login まで起動、dmesg に `boot: HAL initialized successfully. [cpu 4 …]`・`boot: CPUs ready: 4`。`ARM64 SMP 4 CPUs`・`ARM64 SMP cpu N online` の行は dmesg に無い: これらは HAL の `hal_printf`（HAL の console、kernel の log には入らない）なので期待どおり。main の cons.c（2026-10-12、serial は既定で off、display が画面を持った後は捨てる）以降は、display が画面を取る前の画面にだけ出る。今後の確かめは dmesg の `CPUs ready: N` と `[cpu N` で見る。
- (b) 4 本の busy loop が同時に走った（5 秒で各 TIME 00:00:05）、`top -b -n 1` は動く。
- (d) `cpus=1` で login まで、`CPUs ready: 1`。
- (c) BUG-288 の reboot と cksum は通過。(e) は未実施。
