<!-- awesome-plan project=zedbsd record=ws308-p001 -->

# ws308-p001 計測と設計（read・write の syscall の並列性を止める lock）

- 親: [WS308](../ws.md)。Queue: q1650（担当 P1）。
- Status: in-progress（2026-10-28 P1: QEMU の計測と Fable 5.1 High の助言まで。次は 5330 の区間の計測、設計は未着手）
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

## Fable 5.1 High の助言（2026-10-28、P1 が依頼。2026-10-28 ユーザー「P1は、Fable 5.1 Highのサブエージェントに助言を求めてOKです。」「bufferを毎回固定するのではなくて使い回すような方法も検討の余地があると思いますので、Fable 5.1 Highへの質問事項に入れてください。」）

要点と P1 の採否:

1. 5330 の算数: 32 thread / 115k IOPS = 1 I/O あたり約 278 µs、device は約 80 µs。CPU は 2.4 個分（1 I/O 約 21 µs の CPU）しか使っておらず 8 割 idle なので、5330 の律速は spin の CPU ではなく、thread の直列の鎖の待ち（起床・眠る lock の convoy・割り込みの CPU の偏り）。QEMU の標本の割合（heap 55%・command_lock 7 割）は CPU の費用で、5330 の順位を決めない。→ 採用。ただし既存の nvme の数（5330 の「completion to waiter avg 4〜6 µs」）から、完了→起床の遅れは小さい。残りの約 200 µs が post の前か起床の後かを区間で測る。
2. 最初の計測: 1 I/O を 4 区間（syscall の入口→post、post→CQ、CQ→thread の実行、実行→syscall の戻り）に分けて平均・最大を出す、CPU ごとの忙しさと割り込みの CPU、metadata の mutex と `vm->lock` の眠りに落ちた数、heap lock の割り込みを止めた時間。→ 採用（p001 の 5330 の計測。計測の kernel は commit しない一時の物）。
3. heap: まず syscall の I/O の道で確保しない（`struct uaccess_pin` に小さな pages の配列を内に持つ、vector は 1 つの配列、block の 4 KiB の bounce は thread ごとの scratch の page）。per-CPU の magazine は後（IRQ の文脈・保持の memory の扱いが要る）。→ 採用。
4. pin の使い回し（ユーザーの質問）: 透明な pin の cache は、cache の間 `pin_count` が上がったままになり、reclaim・pageout が取れない、fork が `vm_private_page_wait_idle` で「次の syscall まで外れない cache」を待って止まる（または pin された page を fork の時に写す仕組みが要る）、書きの pin は COW を先に割る必要、と io_uring の registered buffers の仕組みと無効化の手順を作り直す事になる。得は競合の無い pin の費用（約 1〜2 µs）だけ。Debian も O_DIRECT は毎回 pin し、buffered は pin しない。登録の UAPI は目標に要らない。→ 採らない（UAPI も足さない）。pin を安くする（大域の mutex と確保を無くす）方を採る。
5. metadata の mutex: pin が要るのは「この vmspace の region と entry の安定」と「検査から最後の page の pin までの同一性」。backing の側（`vm_private_page_pin`）は自分で再検査して EBUSY を返す。全ての region・entry の書き手が `vm->lock` を持つなら `vm->lock` だけで足りる。ただし 1 process の 32 thread は `vm->lock`（mutex）でも並ぶので、vmspace ごとの rwlock（pin・check・copy は共有、map・unmap・mprotect・fork・fault の公開は排他）が現実的な目標。その前に書き手の監査（vmspace-map・unmap・fault・fork（親の entry に COW を付ける時に `vm->lock` を持つか）・reclaim・pageout・swap）と lock の順の確認（今は metadata → vm->lock）。→ 採用（後の Phase、監査と試験を先に）。
6. process->lock は syscall ごとに 2 箇所: credential の読み手の数（process.c:555/577）と signal の検査（signal.c:149、syscall.c:3003）。加えて `user_probe_syscall` が大域の数を全 CPU から書く（user-probe.c:55）。credential は thread ごとの参照と process の世代（setuid は世代を進め、古い世代の thread が居なくなってから解放）、signal は thread ごとの「止めていない pending あり」の要約の語。→ 採用（後の Phase）。
7. NVMe の command_lock: 1 回の submit を 1 回の保持に（begin_bio・slot_acquire・post）、release と end_bio も 1 回に、slot の空きの list、割り込みで起こすのは lock の外で wake_one、doorbell は束ごと。完了の polling は先にしない。queue を複数にするのは 1〜3 の後に 5330 で command_lock が残る時だけ（Debian は 1 queue で 235k）。→ 採用（WS299 の pci-nvme.c、2026-10-28 ユーザー「P1の触る範囲が広がるのはOKです。」）。
8. 順: A 5330 の区間の計測 → B 起床の道（lock の外の起床・起こされた thread の置き場所・idle の C-state）→ C syscall の I/O の道の確保を無くす → D syscall ごとの process->lock を無くす → E vmspace の rwlock の pin → F command_lock の残り・heap の magazine・複数 queue（数が要る時だけ）→ 規約。順は A の数で決める。
9. 要る回帰試験: QD32 の受け入れ、fork と COW の負荷、pread と munmap・mmap・mprotect の競争、記憶の圧迫と swap、setuid の競争、I/O の間の signal と exec、readv/writev（16 iovec・page を跨ぐ）と O_DIRECT の揃わない buffer、boot-test と `tests/scenarios/`。→ 採用（p00x の回帰の Phase の中身）。

## 再開点（2026-10-28、P1）

- 次: 5330 の区間の計測（一時の計測の kernel）を Q1 の OK の後に。続いて設計（B〜E）→ design-reviewer。
