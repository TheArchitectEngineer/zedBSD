<!-- awesome-plan project=zedbsd record=ws308-p006 -->

# ws308-p006 回帰（read・write の syscall の道の変更の正しさ）

- 親: [WS308](../ws.md)。Queue: q1652（担当 P1）。前提: [p002](../phase002/phase.md)・[p003](../phase003/phase.md)。
- Status: cleared（2026-10-28 P1、q1654: 前からの不具合 3 つの結末、signal・exec の競争の試験、回帰の一式と 5330 の 1 回。残りの 2 つの試験は下の「残り」）
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

## 見つかった前からの不具合（WS308 の退行ではない）と結末（2026-10-28、q1654、P1）

1. setuid と I/O: 多 thread の process で seteuid を繰り返し、他の thread が syscall の中に居続けると、`process->retired_creds` に退役した credential が溜まり kernel の heap が尽き、他の process が落ちる。→ [p004](../phase004/phase.md) で直した（thread ごとの credential、c438ea61c・5a2746649）。修正の前の main の image で iostress の setuid が rc=137、今 PASS。
2. fork・munmap と O_DIRECT で user の #GP: kernel の不具合でなく試験の競争だった。gdb で落ちた瞬間を捕まえ、forker の thread の TCB（fs_base 0x100021000）が disk の乱数で埋まっていた事（#GP）、別の回では `__rtld_thread_alloc` が mmap した直後の自分の TLS の page で page fault（unmap 済み）だった事を見た。iostress は mapper の thread を作った後に checker・forker の thread を作るので、mapper の munmap が region に開けた穴に後の thread の stack・TLS の mmap(NULL) が入り、mapper の MAP_FIXED の mmap や reader の pread がそれを上書きする（POSIX 上正しい動作）。→ iostress.c で mapper を最後に作る（210d2ac3d）。直した後、direct の fork＋mapper（40 s）は 6＋3＋3 回、全て PASS（前は 3〜5 回に 1 回落ちた）。cached の hog＋fork＋mapper では、別に子の検査の失敗が数回に 1 回出たので、iostress に子の失敗の理由（exit code・wait status）を足して調べると、`fork()` 自身が memory の圧迫で断られていた（POSIX が許す ENOMEM・EAGAIN）。→ iostress は fork の拒否を別に数える（fork_refused、ENOMEM・EAGAIN 以外は失敗）。データの誤り（子の 4〜6）は一度も無い。
3. 記憶の圧迫と cached の読み: 記録の「EINTR（4）」は誤りで、zedBSD の errno 4 は ENOMEM（EINTR は 7）。空きが cache の予約の下限を割ると共有の cache の予算（`cache_memory_reserve` の optional）が buffer cache の新しい行を全て断り、cache は 1 行まで縮み、cap の下なので行の使い回しもせず、cached の pread が即座に ENOMEM を返していた（gdb で `file_io_transfer` の -ENOMEM、cache_current_bytes 4096 を見た）。→ 今要る 1 行（`acquire_line`、metadata の view の最初の行）は、断られたら一度だけ圧迫の道（同じ disk の空いた clean な行を cap に関係なく使い回し、無ければ必須の予約で cap を越えて取る）を回る。read-ahead と run の追加の行は今までどおり断られてよい。物理の確保の失敗だけ ENOMEM（POSIX の read の「may fail」）。同じ圧迫で UFS の root が「a j3 home block could not be written (error 4)」で書けなくなる事も見た: `buf_writeback_range` が cache に無い行まで確保していた → 無い行は飛ばす（d30136b22）。直した後、cached hog 1200 MiB（nofork nomap）PASS（前は errors 455 万）、boot disk の direct hog・cached hog＋mapper も PASS。
- fork が少ない件（direct の読みが続くと fork が 20〜40 s に 2〜22 回）: 上の不具合とは別の原因の読み。`vmspace_fork` は vmspace の全ての fault が落ち着くまで待ち（`vmspace_wait_faults_locked`、region の hold が 0 の瞬間を待つ）、batch ごとに VM の lock を放し、その間の layout の変化（mapper の munmap・mprotect）で fork を最初からやり直す（EAGAIN）。reader の fault（fork の COW を壊す fault を含む）と mapper が続く間は、その瞬間が来にくく飢える。直すなら fork の間は新しい fault と layout の変更を待たせる（書き手優先）設計の変更になる。2026-10-28 Q1「fork の飢え（vmspace_fork が fault の落ち着きを待って最初からやり直す）は後回しでよい（Q1 の判断）。」→ Future Work（Q1 が future-work.md に足す）。

## signal と exec の競争（2026-10-28、P1）

- `plan/ws308/tests/sigexec.c`・`qemu-sigexec.sh`: 子が 3 つの thread で syscall を回し（SIGUSR1 の handler）、4 つ目の thread（SIGUSR1 を block）が自分を exec する間、親が SIGUSR1・SIGSTOP/SIGCONT を送り続け、4 回に 1 回は乱数の時に SIGKILL。新しい image は handler を置いて SIGUSR1 を unblock して 0 で終わる。20 s を越える回は hang。Linux（host）で PASS（exec_ok 225・killed 75・bad 0・hang 0 / 300）。QEMU の結果は下の表。

## 回帰（2026-10-28、P1、QEMU/KVM、serial の image）

| 試験 | 210d2ac3d（p007 の前） | b8ce0cdc3（p007 の後）と iostress の最後の版 |
| --- | --- | --- |
| iostress fork＋mapper（direct 40 s） | PASS 6/6 | PASS 3/3、3/3 |
| iostress cached hog 1200 MiB＋fork＋mapper（40 s） | PASS 2/2 | fork の拒否（ENOMEM・EAGAIN）を失敗に数えた版で 1/1 と 5/6 が FAIL、拒否を別に数える版で PASS 4/4 |
| iostress 既定（direct・cached・direct setuid・cached hog・boot disk の direct hog setuid、各 20 s）＋4 process の direct の書き | PASS 5/5 | PASS 5/5、5/5 |
| credrace | PASS | PASS |
| sigexec（300 回） | PASS | PASS |
| nvme-shapes・nvme-errors・nvme-timeout | PASS | PASS |
| directio・readahead・grow-reboot | PASS | 未実施（p007 は書き方だけ） |
| boot-test | （sh で呼んで道具の誤り、bash で取り直し） | PASS（build/tmp/s1-boot-test/login.png） |

- 5330（10.0.30.3、b8ce0cdc3 の config-5330 の kernel、qdprobe、同じ時間帯の Debian は debian-qdstat）: 乱読み 4 KiB QD32 direct 224.2k IOPS（918 MB/s）＝Debian 225.8k の 99.3%、process 224.8k＝Debian 225.4k の 99.7%、cached 241.2k（988 MB/s）、QD1 direct 13.9k（Debian 13.6k）。7aa9288b8 の前の数（direct 223.8k、cached 240.4k）から退行なし。syscall だけの null call: thread QD32 610k→1.41M IOPS（syscall ごとの process->lock を外した分）、QD1 5.5M→6.1M、process QD32 9.0M→8.8M（同じ程度）。記録は `build/hw5330/ws308c/`（w308c-q・w308c-z・w308c-d の summary）。

## 残り

- readv/writev の O_DIRECT の揃わない buffer（qemu-directio.sh の範囲を越える分）は未実施。
- setuid の binary の exec を兄弟の thread の seteuid と競わせる試験は未実施（setuid の binary を image に置く道が要る）。
