<!-- awesome-plan project=zedbsd record=ws308 -->

# WS308 VM の metadata の大域の lock の取り合いを無くす（read・write の syscall の並列性）

- Status: incomplete（2026-10-28 新設、q1650 で P1 が p001 から）
- Master: [plan/master.md](../master.md)。Primary Milestone: MG004。関係: [WS299](../ws299/ws.md)（5330 の NVMe の乱読み QD32 が約 115k IOPS で頭打ち、Debian の 48%。この WS の後に WS299 に戻る）。

## ユーザーの決定（原文）

- 2026-10-28 ユーザー「P1にお伝えください。フリーズは再現したときに対策を考え直します。仮想メモリのロックの変更は、P1の担当にして、一時的にそれに対応する別なWSを処理して、そのあとでNVMeのWSに戻りましょう。P1は、Fable 5.1 Highのサブエージェントに助言を求めてOKです。状況を共有して、検討の進め方を助言してもらってください。」

## 目標

read・write・pread・pwrite・vector の syscall が、user の buffer を固定する所（`uaccess_pin` → `vmspace_pin_user_pages`、`src/kern/vm/vmspace-copy.c`）で取る大域の VM の metadata の mutex（`src/kern/vm/metadata-lock.c`）の取り合いを無くし、多 thread・多 process の I/O が CPU の数に応じて伸びるようにする。数の目安は WS299 の乱読み QD32（5330、Debian 964 MB/s の 95% 以上）。

## 事実（2026-10-28 P1 の読み。数は確かめ中）

- 5330 の乱読み QD32 は direct でも cache 経由でも約 115k IOPS で止まり、12 CPU は 8 割 idle。全ての read/write の syscall が `uaccess_pin` で `metadata_lock` を取る。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 計測（lock の takes・waits・保持の時間、QEMU と 5330）と設計（lock の分割・範囲の縮小・lock の要らない道。VM の不変条件の一覧）、Fable の助言、design-reviewer | — | in-progress（q1650） |
| p002 | 実装と host 試験 | p001 | planned |
| p003 | 回帰（QEMU の全 boot、fork・mmap・munmap・exec の負荷、5330 の測定）と全 platform の build | p002 | planned |
| p004 | 規約の全文の見直し | p003 | planned |

## 制約

- HAL の API は変えない（要れば差分の案をユーザーの承認に）。VM は全 platform・全 process に効くので、正しさ（use-after-free・競合）を最優先し、試験で確かめる。
- 2026-10-28 ユーザー「WS308ですが、bufferを毎回固定するのではなくて使い回すような方法も検討の余地があると思いますので、Fable 5.1 Highへの質問事項に入れてください。それによって、WSの内容を変更してもいいです。」→ p001 で pin の使い回し（pin の cache・登録済みの buffer）も Fable に聞いて評価し、結果で目標・Phase を変えてよい（UAPI を足すならユーザーの判断）。
- 2026-10-28 P1 の計測（QEMU 8 CPU）: 主因は metadata_lock でなく kernel heap の大域の lock（entry.c）・NVMe の command_lock・vm_object_registry_lock・process の cred の lock。ユーザー「P1の触る範囲が広がるのはOKです。」→ 目標をこれらの lock に広げる（P1 が目標と Phase を直す）。所有に src/kern/entry.c・process.c・vm の object registry・pci-nvme.c を追加。
