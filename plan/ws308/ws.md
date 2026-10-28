<!-- awesome-plan project=zedbsd record=ws308 -->

# WS308 VM の metadata の大域の lock の取り合いを無くす（read・write の syscall の並列性）

- Status: completed（2026-10-28 P1、q1654。Q1 の merge と確かめで確定）
- Master: [plan/master.md](../master.md)。Primary Milestone: MG004。関係: [WS299](../ws299/ws.md)（5330 の NVMe の乱読み QD32 が約 115k IOPS で頭打ち、Debian の 48%。この WS の後に WS299 に戻る）。

## ユーザーの決定（原文）

- 2026-10-28 ユーザー「P1にお伝えください。フリーズは再現したときに対策を考え直します。仮想メモリのロックの変更は、P1の担当にして、一時的にそれに対応する別なWSを処理して、そのあとでNVMeのWSに戻りましょう。P1は、Fable 5.1 Highのサブエージェントに助言を求めてOKです。状況を共有して、検討の進め方を助言してもらってください。」
- 2026-10-28 ユーザー「P1の触る範囲が広がるのはOKです。」（目標を heap・NVMe の command_lock・process の cred の lock に広げた）
- 2026-10-28 ユーザー「WS308ですが、bufferを毎回固定するのではなくて使い回すような方法も検討の余地があると思いますので、Fable 5.1 Highへの質問事項に入れてください。それによって、WSの内容を変更してもいいです。」（評価して採らなかった、[p001](phase001/phase.md)）
- 2026-10-28 ユーザー（クリック）: iostress で見つけた前からの不具合 3 つは「WS308 の中で P1 が直す (推し)」、p005 は「後回し（Future Work） (推し)」。
- 2026-10-28 ユーザー「P1は、現在原因がわかっていないパフォーマンスの問題については、Fable 5.1 Highに何度でも助言を求めてOKです。」
- 2026-10-28 Q1「fork の飢え（vmspace_fork が fault の落ち着きを待って最初からやり直す）は後回しでよい（Q1 の判断）。」

## 目標

read・write・pread・pwrite・vector の syscall の I/O の道の並列性を止める大域・process 単位の lock と確保を無くし、多 thread・多 process の I/O が CPU の数に応じて伸びるようにする。数の目安は WS299 の乱読み QD32（5330、Debian 964 MB/s の 95% 以上）。

- 2026-10-28 改訂（P1、p001 の計測と Fable 5.1 High の助言による）: 当初の「VM の metadata の mutex」は QEMU の計測で主因でなかった（待ちは取りの 6〜18%、標本の 5%）。対象を、syscall ごとの kernel heap の確保（大域の spin lock）、全 syscall の process->lock（credential・signal）、VM の metadata の mutex と vmspace の lock（pin）、NVMe の command_lock（direct の道）に広げる。2026-10-28 ユーザー「P1の触る範囲が広がるのはOKです。」pin の使い回し（cache・登録の UAPI）は採らない（理由は [p001](phase001/phase.md)）。regular file の VM object の registry lock は block device の目標に要らないので、要れば別の Phase。

## 結果

- 5330（10.0.30.3、同じ時間帯の Debian と）: 乱読み 4 KiB QD32 direct 224.2k IOPS（918 MB/s）＝Debian 225.8k の 99.3%、process 99.7%、cached 241.2k（988 MB/s）。始めの頭打ち（約 115k、Debian の 48%）から倍。目標（Debian の 95% 以上）を満たした。syscall だけの null call は thread QD32 610k→1.41M IOPS。
- 直した所: kernel heap を syscall の I/O の道で取らない（p003）、NVMe の起床を command_lock の外へ・command id で slot を O(1)・空き slot の hint・完了の返却と end_bio を 1 回の保持に、amd64 の timecounter の大域の lock を外す、disk の統計の時計を disk_registry_lock の外へ（p002）、syscall ごとの process->lock を無くす thread ごとの credential（p004）。
- 前からの不具合 3 つを直した（[p006](phase006/phase.md)）: seteuid の繰り返しで退役した credential が溜まり heap が尽きる（p004）、memory の圧迫で buffer cache が新しい行を全て断り cached の pread が ENOMEM（記録の「EINTR」は errno の読み違い）と UFS の journal の home の書きの失敗（d30136b22）、fork・munmap と O_DIRECT の user の #GP は iostress の試験の競争だった（210d2ac3d）。review で見つけた前からの credential の競争（setuid 系の更新が消える、set_id と ptrace の窓、si_uid が euid）も直した。
- 試験: `plan/tools/kstress/`（2026-10-28 移した）の iostress・credrace・sigexec・nvme-errors・nvme-timeout と WS299 の NVMe の試験、boot-test、全 platform の vmunix の build（warning 0）。

## 制限・移管

- p005（pin を大域の metadata の mutex なしで）: ユーザーの決定で Future Work（Q1 が future-work.md に）。
- fork の飢え: direct の読みと mapper が続く間、`vmspace_fork` が「全ての fault が落ち着くまで」待ち、batch ごとに lock を放す間の layout の変化で最初からやり直すので、fork が 20〜40 s に 2〜30 回しか通らない。直すなら fork の間は新しい fault と layout の変更を待たせる設計の変更。Q1 の判断で後回し（Future Work、Q1 が future-work.md に）。
- regular file の VM object の registry lock、heap の per-CPU の magazine、NVMe の複数 queue: 目標の数に要らなかったので行わない。
- 未実施の試験: readv/writev の O_DIRECT の揃わない buffer、setuid の binary の exec を兄弟の thread の seteuid と競わせる試験、NVMe の stop・detach の最中の眠る者。
- 原因の分かっていない計測の食い違い: syscall の外の 4.8 µs（目標には要らない、[p002](phase002/phase.md)）。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 計測（QEMU の標本、5330 の 1 I/O の区間）と設計、Fable の助言、design-reviewer | cleared |
| [p002](phase002/phase.md) | 起床の道と NVMe の command_lock、amd64 の timecounter、disk の統計の時計、buffer cache の 1 行の早い戻り | cleared（15840093d・84fd65ac6・27a02af59・a609f74a1・7aa9288b8） |
| [p003](phase003/phase.md) | syscall の I/O の道で heap を確保しない | cleared（01e701914） |
| [p004](phase004/phase.md) | syscall ごとの process->lock を無くす（thread ごとの credential）と credential の更新の競争 | cleared（c438ea61c・5a2746649） |
| p005 | pin を大域の metadata の mutex なしで | canceled（Future Work、ユーザーの決定） |
| [p006](phase006/phase.md) | 回帰、前からの不具合 3 つ、signal と exec の競争、5330 | cleared（d30136b22・210d2ac3d） |
| p007 | 規約の全文の見直し（WS308 が変えた C の全部、conformance-reviewer） | cleared（b8ce0cdc3。書き方だけ、config-5330・rpi4・m68k-virt で warning 0、QEMU の回帰 PASS） |

## 再開点

- 2026-10-28 P1: WS308 の成果は agent/p1-ws308c（基点 main af37831de、先頭はこの記録の commit）。Q1 の merge の後、P1 は [WS299](../ws299/ws.md)（q1646）の再開点へ戻る。
- 消してほしい物（Q1 に頼む）: 一時の build `build/p1c-*`（k・g・q1〜q4・cr・cr-rpi4・cr-m68k・a・b）、`build/tmp/base-af3`（修正の前の main の写しと image）、`build/tmp/rr/data.bin`、WS308 の完了の形で Phase の directory（phase001〜phase006）。`plan/ws308/tests/` は WS299 と回帰で使う（iostress・credrace・sigexec は plan/tools.md への登録を Q1 に頼む）。
