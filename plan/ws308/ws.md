<!-- awesome-plan project=zedbsd record=ws308 -->

# WS308 VM の metadata の大域の lock の取り合いを無くす（read・write の syscall の並列性）

- Status: incomplete（2026-10-28 新設、q1650 で P1 が p001 から。2026-10-28 P1 が目標と Phase を改訂）
- Master: [plan/master.md](../master.md)。Primary Milestone: MG004。関係: [WS299](../ws299/ws.md)（5330 の NVMe の乱読み QD32 が約 115k IOPS で頭打ち、Debian の 48%。この WS の後に WS299 に戻る）。

## ユーザーの決定（原文）

- 2026-10-28 ユーザー「P1にお伝えください。フリーズは再現したときに対策を考え直します。仮想メモリのロックの変更は、P1の担当にして、一時的にそれに対応する別なWSを処理して、そのあとでNVMeのWSに戻りましょう。P1は、Fable 5.1 Highのサブエージェントに助言を求めてOKです。状況を共有して、検討の進め方を助言してもらってください。」

## 目標

read・write・pread・pwrite・vector の syscall の I/O の道の並列性を止める大域・process 単位の lock と確保を無くし、多 thread・多 process の I/O が CPU の数に応じて伸びるようにする。数の目安は WS299 の乱読み QD32（5330、Debian 964 MB/s の 95% 以上）。

- 2026-10-28 改訂（P1、p001 の計測と Fable 5.1 High の助言による）: 当初の「VM の metadata の mutex」は QEMU の計測で主因でなかった（待ちは取りの 6〜18%、標本の 5%）。対象を、syscall ごとの kernel heap の確保（大域の spin lock）、全 syscall の process->lock（credential・signal）、VM の metadata の mutex と vmspace の lock（pin）、NVMe の command_lock（direct の道）に広げる。2026-10-28 ユーザー「P1の触る範囲が広がるのはOKです。」pin の使い回し（cache・登録の UAPI）は採らない（理由は [p001](phase001/phase.md)）。regular file の VM object の registry lock は block device の目標に要らないので、要れば別の Phase。

## 事実（2026-10-28 P1 の読み。数は確かめ中）

- 5330 の乱読み QD32 は direct でも cache 経由でも約 115k IOPS で止まり、12 CPU は 8 割 idle。全ての read/write の syscall が `uaccess_pin` で `metadata_lock` を取る。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 計測（QEMU の標本、5330 の 1 I/O の区間）と設計、Fable の助言、design-reviewer | — | in-progress（q1650） |
| [p002](phase002/phase.md) | 起床の道と NVMe の command_lock（割り込みの起床を command_lock の外へ、command id で slot を O(1) で引く、amd64 の timecounter の大域の lock を外す。submit・release の lock の回数は次） | p001 | in-progress（実装・review の反映、5330 で direct QD32 124.6k→143.2k IOPS） |
| [p003](phase003/phase.md) | syscall の I/O の道で heap を確保しない（pin の内の配列、block device の bounce を shared の I/O pool から） | p001 | in-progress（実装 01e701914、5330 で cached QD32 139k→172k IOPS（p002 と合わせて）） |
| p004 | syscall ごとの process->lock を無くす（thread ごとの credential の参照と世代、signal の要約の語、user probe の数を CPU ごとに） | p001 | planned |
| p005 | pin を大域の metadata の mutex なしで（vmspace ごとの rwlock、書き手の監査と lock の順） | p001 | planned |
| p006 | 回帰（QD32 の受け入れ、fork/COW・munmap/mprotect と pread の競争・swap・setuid の競争・signal と exec・readv/writev・O_DIRECT、QEMU の全 boot、全 platform の build、5330） | p002〜p005 | planned |
| p007 | 規約の全文の見直し | p006 | planned |

- p002〜p005 の順は p001 の 5330 の区間の数で決める（数が要らない Phase は取り下げる）。heap の per-CPU の magazine・NVMe の複数 queue は、p002〜p005 の後に 5330 で足りない時だけ足す。

## 制約

- HAL の API は変えない（要れば差分の案をユーザーの承認に）。VM は全 platform・全 process に効くので、正しさ（use-after-free・競合）を最優先し、試験で確かめる。
- 2026-10-28 ユーザー「WS308ですが、bufferを毎回固定するのではなくて使い回すような方法も検討の余地があると思いますので、Fable 5.1 Highへの質問事項に入れてください。それによって、WSの内容を変更してもいいです。」→ p001 で pin の使い回し（pin の cache・登録済みの buffer）も Fable に聞いて評価し、結果で目標・Phase を変えてよい（UAPI を足すならユーザーの判断）。
- 2026-10-28 P1 の計測（QEMU 8 CPU）: 主因は metadata_lock でなく kernel heap の大域の lock（entry.c）・NVMe の command_lock・vm_object_registry_lock・process の cred の lock。ユーザー「P1の触る範囲が広がるのはOKです。」→ 目標をこれらの lock に広げる（P1 が目標と Phase を直す）。所有に src/kern/entry.c・process.c・vm の object registry・pci-nvme.c を追加。
