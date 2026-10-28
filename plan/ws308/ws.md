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
| [p002](phase002/phase.md) | 起床の道と NVMe の command_lock（割り込みの起床を command_lock の外へ、command id で slot を O(1) で引く、amd64 の timecounter の大域の lock を外す、空き slot の hint、完了の返却と end_bio を 1 回の保持に）、disk の統計の時計を disk_registry_lock の外へ、buffer cache の 1 行の読みで heap と disk_registry_lock を取らない | p001 | in-progress（a609f74a1、5330 で direct QD32 223.8k IOPS＝同じ時間帯の Debian 225.5k の 99.3%、cached 240.4k。回帰の残りあり） |
| [p003](phase003/phase.md) | syscall の I/O の道で heap を確保しない（pin の内の配列、block device の bounce を shared の I/O pool から） | p001 | in-progress（実装 01e701914、5330 で cached QD32 139k→172k IOPS（p002 と合わせて）） |
| p004 | syscall ごとの process->lock を無くす（thread ごとの credential の参照と世代、signal の要約の語、user probe の数を CPU ごとに） | p001 | planned（取り下げの候補: I/O の QD32 は thread と process で同じで、数の目標は p002・p003 で達した。Q1・ユーザーの判断待ち） |
| p005 | pin を大域の metadata の mutex なしで（vmspace ごとの rwlock、書き手の監査と lock の順） | p001 | planned（取り下げか後回しの候補: 待ちは取りの 8〜15% だが数の目標は達した。Q1・ユーザーの判断待ち） |
| p006 | 回帰（QD32 の受け入れ、fork/COW・munmap/mprotect と pread の競争・swap・setuid の競争・signal と exec・readv/writev・O_DIRECT、QEMU の全 boot、全 platform の build、5330） | p002〜p005 | planned |
| p007 | 規約の全文の見直し | p006 | planned |

- p002〜p005 の順は p001 の 5330 の区間の数で決める（数が要らない Phase は取り下げる）。heap の per-CPU の magazine・NVMe の複数 queue は、p002〜p005 の後に 5330 で足りない時だけ足す。

## 制約

- HAL の API は変えない（要れば差分の案をユーザーの承認に）。VM は全 platform・全 process に効くので、正しさ（use-after-free・競合）を最優先し、試験で確かめる。
- 2026-10-28 ユーザー「WS308ですが、bufferを毎回固定するのではなくて使い回すような方法も検討の余地があると思いますので、Fable 5.1 Highへの質問事項に入れてください。それによって、WSの内容を変更してもいいです。」→ p001 で pin の使い回し（pin の cache・登録済みの buffer）も Fable に聞いて評価し、結果で目標・Phase を変えてよい（UAPI を足すならユーザーの判断）。
- 2026-10-28 P1 の計測（QEMU 8 CPU）: 主因は metadata_lock でなく kernel heap の大域の lock（entry.c）・NVMe の command_lock・vm_object_registry_lock・process の cred の lock。ユーザー「P1の触る範囲が広がるのはOKです。」→ 目標をこれらの lock に広げる（P1 が目標と Phase を直す）。所有に src/kern/entry.c・process.c・vm の object registry・pci-nvme.c を追加。

## 再開点（2026-10-28、P1、q1650 を 2026-10-28 ユーザー「5時間セッション上限が近いのでP1, P2は13時まで待機してください。」で区切り）

- commit（agent/p1-ws308）: 3ec112dc5・7b6a2f4a3・f2d01c3e5（p001 の計測と Fable の助言）、15840093d（p002）、01e701914（p003）、84fd65ac6（p002 の review の反映）、27a02af59（command_lock の回数）、この記録の commit。
- 今の数（5330、一時の計測の kernel）: 乱読み 4 KiB QD32 direct 214.5k IOPS・878 MB/s（Debian 964 の 91%）、cached 204.8k・839 MB/s（87%）。目標の 95% まであと少し。
- 次の一手: (1) direct の残り（入口→post 6〜9 µs、post→CQ 126 µs は device の側か割り込みの束ね方）と cached の pin→submit 12 µs（buffer cache の道: disk_lock・cache_lock）の内訳を同じ計測（`build/tmp/exp-io-2.patch`・`build/tmp/exp-io.h` を当てて build、`nvme-bench-5330.sh zedbsd NAME VMUNIX qdprobe`）で見る。(2) submit の command_lock をさらに減らす（slot_acquire と post を 1 回の保持に、release と end_bio を 1 回に）。(3) metadata の mutex の待ち（取りの 5〜15%）→ p005。(4) p004（process->lock）は I/O の QD32 が thread と process で同じなので、この目標には効かない見込み（取り下げ候補、数で判断）。(5) 27a02af59 の他の 4 config の build、回帰の試験の残り（timeout・stop・detach の最中、TSC の無い機械）、p006 の負荷試験、p007 規約。
- 5330 は Debian に戻した（lock は返した）。

## 再開点（2026-10-28、P1、q1652）

- commit（agent/p1-ws308b、基点 main 9fda28faf）: a609f74a1（NVMe の空き slot の hint・遅延の計測の間引き・完了の返却と end_bio を 1 回の保持に、disk の統計の時計を disk_registry_lock の外へ、transfer_run の 1 行の早い戻り、Debian の測り直しの道具 `plan/ws308/tests/debian-qdstat.sh`）と、この記録の commit。
- 数（5330、同じ時間帯）: 乱読み 4 KiB QD32 direct 223.8k IOPS（916.7 MB/s）＝Debian 225.5k（923.5 MB/s）の 99.3%、process 99.6%、QD1 105%、cached QD32 240.4k（984.6 MB/s、以前の基準 964 の 102%）。今日の Debian は以前の 964 MB/s でなく 923 MB/s（[p002](phase002/phase.md)）。→ 数の目標（Debian の 95% 以上）は達した。
- 残り: (1) 回帰の試験（timeout・stop・detach の最中に slot に眠る者、TSC の無い機械の起動）と p006 の負荷（fork/COW・munmap/mprotect と pread の競争・swap・setuid・signal と exec・readv/writev）、(2) p004・p005 を取り下げるか（Q1・ユーザー）、(3) p007 規約の全文の見直し、(4) syscall の外の 4.8 µs の計測の食い違い（p002 の「計測の kernel」の項、目標には要らない）。a609f74a1 の design-reviewer は依頼中。
