<!-- awesome-plan project=zedbsd record=ws134-p005 -->
# ws134-p005: K1 kernel の CPU ごとの時間 `hw.cputimes`

Status: in-progress（q661、P2 generation8、2026-10-04。実装・build 済み、T の QEMU の試験待ち）
Disposition: normal
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §1.1・§1.2 の K1
依存: なし（HAL の API は変えない。2026-10-04 Q1「uapi の構造の追加は UAPI で HAL ではないので進めてよい（include/hal/ に触れるなら止めて Q1 へ）」）

## 範囲

CPU ごとの user・system・idle・other の tick を kernel が数え、sysctl `hw.cputimes` で出す。sysctl の CLI の表示と `top` の `%Cpu(s)` の行。
monitor の本物の値（p008）の出どころの一つ。interrupt は分けない（HAL の API が要る、design.md §1.2 の review 4）。

## 実装（commit 670647c、統合 Q1）

- `include/uapi/sysctl.h`: `HW_CPUTIMES 6`、`CPU_TIMES_VERSION 1`、`struct cpu_times_header`（version・struct_size・element_size・count・hz・reserved、24 byte）
  と `struct cpu_times_entry`（user・system・idle・other の u64、32 byte）、ILP32・LP64 の大きさの `_Static_assert`。値は header の後に CPU の数だけ
  entry。読み取りだけ、小さい buffer は ENOMEM と要る長さ。
- `src/kern/sched.c`: `struct sched_cpu` に `times[SCHED_TIMES]`（その CPU の tick だけが足し、他は atomic に読む）。`sched_clock_cpu` で
  `charge_cpu_time`: thread 無し → other、idle の thread → idle、`state != THREAD_RUNNING` → other（眠りかけ・終わりかけ）、process 無し・
  process0 → system、`accounting_kernel_depth != 0` → system、他 → user。公開の `sched_cpu_time(cpu, &time)`（`include/kern/sched.h` の
  `struct sched_cpu_time`）。
- `src/kern/sysctl.c`: leaf `hw.cputimes`、`sysctl_cputimes`（呼び手の kernel の buffer に header と CPU ごとの entry を直に組む。hz は
  `KERN_CLOCK_HZ`）。
- `userland/base/sysctl/main.c`: `show_cputimes`（`hw.cputimes: hz= cpus=` と CPU ごとの行）、`sysctl -a` にも。
- `userland/base/top/main.c`: `%Cpu(s): us, sy, id, ot`（全 CPU の和の前の描画からの差、最初は boot から）。前は 0.0/100.0 の固定の文字だった。

## 確かめ

- build: `make ZEDBSD_CONFIG=plan/ws134/tests/config-amd64-monitor.mk BUILD=build/p2-q660 build/p2-q660/vmunix build/p2-q660/bin/top
  build/p2-q660/bin/sysctl` exit 0、warning 0（kernel は -Werror）。Q1 の統合でも kernel の warning 0、include/hal 不変。
- style-check: 変えた file の新しい関数に違反 0（sched.c・sysctl.c の既存の違反の数は変わらない）。
- QEMU（T に依頼）: `plan/ws134/tests/cputimes-p005.sh`（4 CPU の guest、SSH で: header と hw.ncpu、静かな 5 秒の和が 5 s×hz×CPU の ±10% で
  idle が半分超、awk の busy loop 1 本で user が 1 CPU の 5 秒の 60% 以上、CPU の数だけの loop で user が全体の 60% 以上と 2 CPU 以上が半分超、
  `top -b -n 2 -d 1` の `%Cpu(s)` の 2 行）。結果は未着。boot は同じ試験の guest の起動で見る。
- 他の arch の kernel: `config/ci/config-pcat.mk`（i386、`BUILD=build/p2-pcat vmunix`）と `config/ci/config-rpi4.mk`（arm64）が exit 0・
  warning 0（u64 は atomic の関数で読み書き）。試験の image: `build/p2-q660/hdd-image.img`（9b8062a）を T1 に渡した。実機は未実施。

## stub の項目

この Phase は kernel だけで、monitor の stub は増減しない（monitor が `hw.cputimes` を読むのは p008）。

## 結果（T1-064、QEMU Venus KVM、main 1587d3d の image、2026-10-04）

`cputimes-p005: PASS`。4 CPU の guest が boot（SSH 13 s）。hz=1000 cpus=4（hw.ncpu 4）、静かな 5 秒の和 20022/20000・idle 20008、
busy loop 1 本で user 4995（cpu 0）、4 本で user 20002・4 CPU とも半分超、top の `%Cpu(s)` 2 行（11.2 us・25.0 us）。
証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-064/out/`。clearance は Q1 の判定。実機は未実施。
