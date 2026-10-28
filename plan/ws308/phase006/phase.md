<!-- awesome-plan project=zedbsd record=ws308-p006 -->

# ws308-p006 回帰（read・write の syscall の道の変更の正しさ）

- 親: [WS308](../ws.md)。Queue: q1652（担当 P1）。前提: [p002](../phase002/phase.md)・[p003](../phase003/phase.md)。
- Status: in-progress（2026-10-28 P1: NVMe の失敗・timeout・停止の道と負荷の道具まで。負荷で前からの不具合が 3 つ見つかった（下）。WS308 の変更による退行は見つかっていない）
- Disposition: normal

## 道具

- `plan/ws308/tests/qemu-nvme-timeout.sh IMAGE`: 2 台目の NVMe を QEMU の throttle（既定 2 IOPS）の下に置き、32 thread の direct の乱読みで driver の 10 s の timeout を起こす（眠る者 32、queue の作り直し、再発行）。続けて guest が使えるかと、boot disk の QD32 の I/O の最中の reboot（controller の停止）で QEMU が 300 s 以内に終わるか。
- `plan/ws308/tests/iostress.c`（randread と同じ形で build）と `qemu-iostress.sh IMAGE`（`IOSTRESS_RUNS` で run を選ぶ）: 8 thread の pread（4 KiB と page を跨ぐ 8 KiB）の最中に munmap・MAP_FIXED の mmap・mprotect、preadv（16 vector、page を跨ぐ）と pread の比較、fork（子が private な領域に読み、周りと data を確かめ、親の page の COW を確かめる）、`hog MIB`（記憶の圧迫）、`setuid`（seteuid を 1 と 0 で往復）。最後に 4 process の O_DIRECT 4 KiB の書き＋fsync（flush の admission と書きの end_bio の競争）。Linux で PASS（host の file）を確かめた。
- 比べる基準の image: WS308 の前の main（75188080b）を `git archive` で `build/tmp/base-src` に出して同じ config で作った（`build/tmp/base-src/build/b3/hdd-image.img`）。

## 結果（2026-10-28、P1、QEMU/KVM、serial の image、a609f74a1＋7aa9288b8）

- `qemu-nvme-timeout.sh` PASS 2 回（timeout は 30〜31 s で戻り randread は失敗で終わる、guest 生存、I/O の最中の reboot は 1 s で reset）。
- DMA を 32 bit に絞って bounce を強いた image（一時の build `build/p1-q3`、source は戻した）で `qemu-nvme-shapes.sh` PASS（`bounced=4096 direct=0`。p002 の (e) の bounce の道の `nvme_io_slot_release_end`）。
- TSC の bit を消した CPU（TCG、`-cpu qemu64,-tsc,-invtsc`、`build/tmp/qemu-notsc.sh` で boot-test の -cpu を差し替え）で `boot-test.sh` PASS。timecounter が available=0 になった事は見ていない。
- iostress（各 20 s）:

| run | 今（a609f74a1＋7aa9288b8） | WS308 の前（75188080b） |
| --- | --- | --- |
| direct（mapper・fork なし） | PASS | PASS |
| direct、fork あり・mapper なし | PASS | PASS |
| direct、mapper あり・fork なし | PASS | PASS |
| direct、mapper と fork | PASS 2 回、process が user の #GP で落ちる 1 回 | 落ちる（#GP、`__rtld_pthread_private` の TLS の読み） |
| cached、setuid | 別の process（shell など）が命令の取り込みの page fault で落ち、guest が答えなくなる | 同じ |
| cached、hog 1200 MiB（guest 1 GiB） | 読みが EINTR（4）を返す（signal は無い） | 同じ |
| direct（boot disk）、hog 1200 MiB | PASS | FAIL（checker の失敗） |
| 4 process の direct の書き＋fsync | PASS | PASS |

## 見つかった前からの不具合（WS308 の退行ではない。Bug にするかは Q1）

1. setuid と I/O: 多 thread の process で seteuid を繰り返し、他の thread が syscall の中に居続けると、他の process が命令の取り込みの page fault（SIGSEGV vector 14、address＝pc）で落ち、guest の shell が消える。P1 の読み（未確認）: `process_cred_read_leave`（src/kern/process.c:577）は reader が 0 になった時にだけ退役した credential を解放する。reader が常に居ると `process_cred_replace` ごとの reservation と credential が `retired_creds` に溜まり続け、kernel の heap が尽きて他の process の fault の処理が失敗する。p004（thread ごとの credential の参照と世代）がこの形を直す。
2. fork・munmap と O_DIRECT: 多 thread の process で direct の読み・munmap/mmap・fork を同時に流すと、時々 process が user の #GP（TLS の control block の読みで壊れた値）で落ちる。前の main でも出る。原因は未解析。
3. 記憶の圧迫と cached の読み: 1 GiB の guest で 1200 MiB を触り続ける子がいると、block device の cached の pread が signal なしで EINTR を返す。前の main でも出る。
- 他: direct の読みを 8 thread が続けると fork が 20 s に 2〜7 回しか通らない（pin の解けるのを待つ。前の main も同じ）。

## 残り

- 不具合 1〜3 を Bug にするか、p004 を取り下げずに 1 の直しとして行うか（Q1・ユーザー）。
- signal と exec の競争、readv/writev の O_DIRECT の揃わない buffer（qemu-directio.sh の範囲を越える分）は未実施。
