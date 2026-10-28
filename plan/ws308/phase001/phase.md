<!-- awesome-plan project=zedbsd record=ws308-p001 -->

# ws308-p001 計測と設計（read・write の syscall の並列性を止める lock）

- 親: [WS308](../ws.md)。Queue: q1650（担当 P1）。
- Status: in-progress（2026-10-28 P1: 計測済み、Fable 5.1 High の助言を待つ、設計は未着手）
- Disposition: normal

## 計測（2026-10-28、P1、QEMU/KVM 8 vCPU、serial の image、`randread`）

道具:
- `plan/ws308/tests/qemu-sample.sh IMAGE VMUNIX SETUP LOAD [SAMPLES]`: guest に負荷を流し、gdbstub で guest を SAMPLES 回止めて全 CPU の内側の frame（`bt 4`）を取り、関数ごとの標本の数と呼び手を表にする（標本の profiler）。
- `randread`（`plan/ws299/tests/randread.c`）に `procs`（thread でなく process）・`notime`（時計を呼ばない）・`nullcall`（pread の代わりに getpid）を足した。
- `metadata_lock` の取り・待ち・保持の数は、一時的に `vm_metadata_enter` に数を足した kernel で測った（commit しない、patch は worktree の `build/tmp/metadata-exp.patch`）。kern.perf=1 は QEMU の KVM では「the platform has no counter」で使えなかった。

| 負荷（4 KiB、notime） | 1 thread | 8 thread（1 process） | 8 process |
| --- | --- | --- | --- |
| getpid だけ | 5.7M/s | 362k/s | 8.8M/s |
| 同（process_cred_read_enter/leave を外した実験の kernel） | 6.8M/s | 1.02M/s | 9.2M/s |
| UFS の file（VM の cache に載った 64 MiB）の pread | 139k/s | 254k/s | 221k/s |
| block device の cached（buffer cache に載った 64 MiB）の pread | 308k/s | 233k/s | — |

- metadata_lock: 8 thread の file の読みで、取り 1 回の読みあたり約 2 回、そのうち 6〜18% が待ち、平均保持 約 700 TSC cycle。主因ではない。
- 標本（80 回 × 8 CPU）:
  - block の cached・32 thread: `kernel_heap_lock_enter` 55%（kern_free 32%・kern_malloc 23%。syscall ごとに `uaccess_pin` の pages 配列の `kern_calloc`（src/kern/uaccess.c:178）と `syscall_io_buffer` の 4 KiB の `kern_malloc`（src/kern/syscall.c:2838、`SYSCALL_IO_CHUNK` 512 を越える非 regular の file）。heap は大域の test-and-set の spin lock（src/kern/entry.c:416）の下の first-fit（src/kern/heap.c:141））、buf_read 7.7%、metadata の mutex 5.3%、process_cred_read_leave 3.4%、disk_lock 2%。
  - block の O_DIRECT・32 thread（QEMU の NVMe で device が律速）: ほぼ全てが NVMe の `command_lock` の取り合い（nvme_io_start 26%・nvme_io_begin_bio 23%・spin 14%・nvme_irq 8%・wait 7%）。1 I/O で command_lock を約 7 回取り、割り込みは CQ の処理と起床を lock の下で行う（src/drivers/pci/pci-nvme.c:4047・4128・4619・4793・4957・5066・5102）。
  - UFS の file の cached・8 thread: `vm_object_registry_lock`（大域の spin、src/kern/vm/object.c:653）29%、vm_object_read_coherent_useful 22%。
- 5330（WS299 p010 の数）: QD32 は direct 467〜474 MB/s・cached 424〜445 MB/s、CPU 12 個は約 8 割 idle、post→完了 平均 80 µs、出ている command 平均 8〜9 本。

## 分かった事

- WS308 の当初の仮説（大域の VM metadata の mutex が主因）は QEMU の数では否。read・write の syscall の並列性を止めるのは、(1) syscall ごとの heap の確保と大域の heap lock、(2) NVMe の command_lock（direct の道）、(3) 全 syscall の process->lock（credential の読み手の数）、(4) regular file の VM object の registry lock、(5) metadata の mutex（小）。5330 の実機での順位は未確認（QEMU の NVMe は command_lock の保持を長く見せる可能性）。

## 再開点（2026-10-28、P1）

- Fable 5.1 High に助言を依頼中（pin の使い回しを含む）。次: 助言の要点と採否 → ws.md の目標・Phase の案を Q1 に → 設計 → design-reviewer。
