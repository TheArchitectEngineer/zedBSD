<!-- awesome-plan project=zedbsd record=ws308-p002 -->

# ws308-p002 起床の道と NVMe の command_lock、amd64 の timecounter の大域の lock

- 親: [WS308](../ws.md)。Queue: q1650（担当 P1）。前提: [p001](../phase001/phase.md) の 5330 の区間の計測。
- Status: cleared（2026-10-28 P1: a609f74a1・7aa9288b8。5330 で direct QD32 224.2k IOPS＝同じ時間帯の Debian 225.8k の 99.3%（b8ce0cdc3、[p006](../phase006/phase.md)）。timeout の最中の眠る者と I/O の最中の reboot（qemu-nvme-timeout.sh）、TSC の bit の無い CPU の起動は PASS。stop・detach の最中の眠る者だけの試験は未実施）
- Disposition: normal
- 範囲の決定: 2026-10-28 ユーザー「P1の触る範囲が広がるのはOKです。」→ Q1: src/drivers/pci/pci-nvme.c（command_lock）を WS308 の所有に。Q1: src/hal/amd64/bsp-pcat/timecounter-policy.c の実装の直し（hal.h の宣言・契約は変えない）を WS308 の所有に、条件「時刻の単調性（CPU をまたいでも戻らない）と、TSC が信頼できない機械・QEMU の fallback を壊さない事を、host 試験か QEMU の多 CPU で確かめる。他の platform の timecounter には触らない。」

## 今の事実（file:行は 2026-10-28 の main 7b6a2f4a3）

- 5330 の QD32 direct: 入口→post 70 µs、post→CQ 92 µs（QD1 64）、CQ→waiter 36 µs（QD1 1）、起床→戻り 43 µs（QD1 1）（p001）。
- NVMe の割り込み `nvme_irq`（pci-nvme.c:4128）は `command_lock` を持ったまま CQ を空にし、1 完了ごとに `nvme_io_completion_owner_locked`（:4022、63 slot の線形の探し）で slot を見つけ、`waitq_wake_all(&slot->waitq)` と `waitq_wake_all(&controller->io_state_waitq)`（:4107-4108）で起こす（scheduler の起床・IPI を lock の下で）。その間、submit（`nvme_io_begin_bio` :4619、`nvme_io_slot_acquire` :4793、`nvme_io_post`）と完了後（`nvme_io_slot_release` :5066、`nvme_io_end_bio` :4714）の thread は command_lock を待つ。
- command id: `nvme_io_next_command_id_locked`（:4766）は試しごとに 63 slot を探す（`nvme_io_command_id_in_use_locked`）。
- `waitq` は自分の lock を持たず、sleeper の condition lock が list を守る（src/kern/waitq.c:55-80）。今の slot の waitq の condition lock は command_lock。
- amd64 の timecounter の読み（`hal_rtc_read_counter` → `amd64_timecounter_read` timecounter.c:755 → `amd64_timecounter_read_guarded` timecounter-policy.c:405）は、割り込みを止め、大域の exchange lock（`state->lock`）を取り、TSC を読み、大域の `last_sample` に compare-exchange の max（`amd64_timecounter_monotonic_max` :361）を書く。NVMe の測る口（post・完了・起床）と disk の統計（`disk_stats_start/end`、1 BIO に 2 回）が毎 I/O 読むので、1 I/O で約 5 回。`monotonic_max` の comment のとおり、CAS の max だけで「戻らない」は成り立つ（外の lock が無くても）。publish・disable は起動の時だけ（timecounter.c:630・688・727・744）。

## 設計

### D1. timecounter の読みから大域の exchange lock を外す（timecounter-policy.c）

- `amd64_timecounter_read_guarded` から `state->lock` の取得と解放を除く。`available` を acquire で読み、`frequency_hz` を読み、sample を取り、`amd64_timecounter_monotonic_max` で `last_sample` を進める（今と同じ CAS）。単調性: CAS の max は、どの CPU の後の読みも、それより前に返った値（＝ last_sample に載った値）より小さくならない（返す値は自分の sample か、それより大きい published の max）。
- 「戻った」の log（KVM の skew の診断）は、sample の前に読んだ `last_sample`（`seen`）より sample が小さい時だけ出す。lock が無いと、先に sample を取った CPU の CAS が後の CPU に負けて clamp に入るが、それは戻りではない（design-reviewer M1）。
- `frequency_hz` は publish の時に `available` の release の前に書かれ、available の間は変わらない（publish は available の時に断る、:329）。disable の後の publish は起動の時の経路だけ。lock の無い読み手は available を acquire で見てから frequency を読むので、available=1 の間の frequency は一つ。
- 割り込みを止める wrapper（timecounter.c:766）は残す（同じ CPU の割り込みとの順は今と同じ）。`state->lock` の field は残し（struct の配置を変えない）、init の 0 の store も残す。
- KVM の skew の clamp（REGRESS の log）は今と同じ。TSC の無い・不安定な機械は `available` が 0 のまま（今と同じ「no counter」）。

### D2. NVMe の割り込みが command_lock の外で起こす（pci-nvme.c）

- slot ごとに `wait_lock`（spinlock、rank `LOCK_RANK_DISK`＝command_lock の `LOCK_RANK_DEVICE` より後）を足し、`slot->waitq` の condition lock を command_lock から `wait_lock` に変える。順: command_lock → wait_lock（逆は取らない）。
- 起こす側: `nvme_io_slot_wake(slot)` = wait_lock の下で `waitq_wake_all(&slot->waitq)`。
  - 割り込み（`nvme_irq_io_locked`）: command_lock の下で state を CQ_DONE・FAULTED にし、slot を手元の配列（最大 `NVME_IO_MAX_SLOTS`）に積む。`nvme_irq` が command_lock を放した後に、積んだ slot を順に `nvme_io_slot_wake`。`io_state_waitq` の起床は command_lock の下に残す（condition lock は command_lock のまま、待つ者がいなければ数を足すだけ）が、1 pass に 1 回にまとめる。
  - 失敗の道（`nvme_io_fail_all_locked` :3940 付近・停止 :3230 付近）: command_lock を持ったまま `nvme_io_slot_wake`（順を守る）。
- 待つ側（`nvme_io_wait_completion`）: command_lock の下で state が ACTIVE なら、眠れる thread は command_lock を放し、wait_lock の下で「state が ACTIVE の間、waitq の sequence を取り `waitq_sleep(&slot->waitq, &slot->wait_lock, …, deadline)`」を回し、抜けたら command_lock を取り直して今と同じ後処理（ETIMEDOUT で ACTIVE なら fail_all、latency の計測、COPYING へ、recovery の owner）。state の読みは `__atomic_load_n`（書きは command_lock の下、起床はその後に wait_lock の下なので、wait_lock の下の読みが ACTIVE を見て眠れば必ず起こされる）。idle の thread（起動の早い時期）は今の poll の道（`nvme_io_wait_locked` の sched_yield、waitq を触らない）のまま。
- 必須の条件（design-reviewer M5）: (a) wait_lock は全ての取る側で `spin_lock_irqsave`（割り込みが同じ CPU で取る）。(b) waiter は wait_lock を放してから command_lock を取る（`waitq_sleep` は condition lock を持って戻る）。(c) 割り込みの遅らせた起床は `irq_busy` を下げる前（detach は `nvme_irq_drain` で irq_busy==0 を待ってから slot を放す）。wait_lock と waitq は controller の初期化で全 slot 分を一度だけ初期化する（design-reviewer L4）。
- 時刻の読み（`nvme_now_ns`）は command_lock の外: post は lock の前、割り込みは lock の前に 1 回読み、その pass の完了に使う（design-reviewer M2）。
- 正しさ: slot の waitq に眠るのは、その slot を持つ 1 本の waiter だけ（:4986 だけが眠る）。起こす側が wait_lock を取る時点で waiter が既に slot を release・再取得していても、新しい waiter は state が ACTIVE なら眠り直す（偽の起床は loop で吸収）。slot の memory は controller の配列で、detach まで残る。

### D3. command id と slot の対応を O(1) に（pci-nvme.c、純関数は include/drivers/pci/pci-nvme-protocol.h）

- command id = `(世代 << 6) | (slot の番号 + 1)`（番号は 0〜62、`NVME_IO_MAX_SLOTS` 63）。slot ごとの 10 bit の世代を取得ごとに進める（`drv_nvme_io_next_command_id`）。0 にならず、出ている command の間で一意（番号が一意）。FFFFh（error log の「command 無し」）になる世代は飛ばす（design-reviewer M3）。
- 前提（design-reviewer L3）: slot ごとに世代は 1024 回で一巡し、epoch は id に入らない。recovery は queue を作り直して CQ を 0 で埋めるので、前の epoch の完了が後から来る事は無い。
- 完了の持ち主は `(cid & 63) - 1` の slot を見て、今と同じ条件（ACTIVE・posted・epoch・command_id 一致）を確かめる。`nvme_io_next_command_id_locked`・`nvme_io_command_id_in_use_locked` の線形の探しを無くす。
- admin queue の command id は変えない。

### 採らない・後に回す

- submit・release の command_lock を 1 回の保持にまとめる、空きの slot の bitmap: D1〜D3 の後の 5330 の区間の数で要否を決める。
- 複数の I/O queue: Debian は 1 queue でも 235k（p001 の Fable の助言）。後。

## 試験

- host: `plan/ws308/tests/host/timecounter-test.c`（timecounter-policy.c を host で build、複数 thread が別々に skew した sample を読み、各 thread の値が戻らない・最後の last_sample が全 sample の max・返した値が他の thread の前に返した値より小さくならない（大域の順の記録で）、publish の前は false）。`plan/ws308/tests/host/nvme-cid-test.c`（cid の作りと戻し: 0 にならない、番号の一意、世代の一周、範囲の外の番号を断る）。
- build: config-5330・pcat・visionfive2・rpi4・m68k-virt で warning 0。
- QEMU: boot-test、`qemu-nvme-shapes.sh`・`qemu-directio.sh`（WS299）、多 CPU の QEMU で randread QD32 direct・cached（nvme の待ち・起床の道）、timecounter は QEMU（KVM の clamp の道）で `clock_gettime` と kern の時刻が戻らない事（多 thread の randread と同時に）。
- 5330: p001 と同じ区間の計測（計測の kernel は一時の物）で比べる。

## レビュー（2026-10-28、design-reviewer）と反映

- High 0。M1（偽の REGRESS の log）→ 反映（`seen`、host 試験に「skew なしで log 0」、HEAD の旧版で FAIL する事を確かめた）。M2（timecounter の CAS の共有の line は残る、command_lock の中の時刻の読み）→ 時刻の読みを lock の外へ。CAS の max を省く案は単調性の条件に関わるので採らない。M3（CID FFFFh）→ 反映（host 試験に足した）。M4（TSC の無い・TCG の試験が無い）→ QEMU の TCG の多 CPU で boot-test を足す。user の API に TSC の時刻は出ない（`clock_gettime` は tick）ので、guest の単調性の試験は作れない。kernel の単調性は host 試験と起動の検査（`runtime_read_check`）で見る。M5 → 設計に明記・comment。M6（失敗の道の試験が無い）→ `plan/ws308/tests/qemu-nvme-errors.sh`（blkdebug で sector 1024 の読みを毎回 EIO、32 thread の direct の乱読みが誤りの完了を受ける）を足した。timeout・stop・detach の最中の眠る者の道は未実施。L1（bitmap）は採らない（配列で足りる、溢れは直に起こす）。L2（state の書きの atomic の揃え）は未対応（uint8_t、順は lock で成り立つ）。L4 → 反映。L5（CSTS の読み）は後。L6 → ws.md の行を直した。L7 → 反映（M1 の assert）。

## 5330 の区間の計測（2026-10-28、p002＋p003 の commit 01e701914 に一時の計測の差分、`w308-b`）

| 負荷 | p001（main） | p002＋p003 | 入口→post | post→CQ | CQ→waiter | 起床→戻り |
| --- | --- | --- | --- | --- | --- | --- |
| direct QD32（thread） | 124620 IOPS | 143242 | 68〜69 µs | 93 µs | 30〜31 µs | 36〜39 µs |
| direct QD32（process） | 124275 | 134575 | 58〜60 µs | 95〜99 µs | 27〜28 µs | 36〜37 µs |
| cached QD32（thread） | 139233 | 172477（706 MB/s、Debian 964 の 73%） | 52〜60 µs | 98 µs | 19〜20 µs | 26〜27 µs |
| cached QD32（process） | 136333 | 167228 | 40〜62 µs | 94〜103 µs | 17〜25 µs | 23〜32 µs |

- heap lock の待ちは 0（direct）・1%（cached）に減った。CQ→waiter と起床→戻りは縮んだが、入口→post は残る。metadata の mutex の待ちは取りの 5〜15%。
- 次: submit の command_lock の回数（begin_bio・slot_acquire・post を 1 回の保持に）、入口→post の内訳（disk_lock・metadata・process->lock）。

## 試験の結果（2026-10-28、review の反映の後の tree）

- host: `timecounter-test` PASS（skew なしの 8 thread で REGRESS の log 0、skew ありで値が戻らない・大域の max が返した値の max。review の前の版（HEAD の timecounter-policy.c）では「a regression was logged without skew」で FAIL する事を確かめた）、`nvme-cid-test` PASS（FFFFh を飛ばす）、WS299 の `nvme-prp-test`・`nvme-hmb-test` PASS。
- build: config-5330（`build/p1-w308b`）・pcat・visionfive2・rpi4・m68k-virt で rc 0・warning 0。
- QEMU（serial の image `build/p1-w308q`）: boot-test（KVM）PASS、boot-test（`QEMU_NO_KVM=1`、TCG の 4 CPU、PIT 較正の TSC の道）PASS、`qemu-nvme-shapes.sh` PASS、`qemu-directio.sh` PASS、`qemu-nvme-errors.sh` PASS（誤りの完了を 3 回、誤りの無い範囲の direct・cached の QD32 6 回、guest は生きている）。
- 未実施: timeout・stop・detach の道に眠る者がいる場合、TSC の無い機械（`available=0`）の起動。

## command_lock の回数を減らす（2026-10-28、P1、27a02af59）

- 区間の計測（`w308-c`、計測の点を足した一時の kernel）: direct QD32 の入口→post 約 66 µs の内訳は pin 5〜7 µs、pin→submit 2 µs、`nvme_io_begin_bio` 27〜29 µs、begin→post（slot の取得・command の組み立て・post）29〜32 µs ＝ command_lock の待ち。1 I/O で command_lock を約 10 回取っていた（ensure_online、begin_bio、pipeline の誤りの見回り（pending 0 でも）、slot_acquire、post、wait（眠った後の取り直し）、dma_uncertain の確認、slot_release、end_bio）＋割り込みの CSTS の読み（device の register の読みを lock の下で毎回）。
- 直し: (1) begin_bio の中で online の条件を同じ保持で見て、外れた時だけ `nvme_io_ensure_online`、(2) pipeline の誤りの見回りは出ている chunk がある時だけ、(3) dma_uncertain と fence は `nvme_io_wait_completion` の保持の中で返す、(4) 割り込みの CSTS の読みは完了を 1 つも取らなかった時だけ（完了を返す controller は fatal ではない。完了が止まる fatal は次の割り込みか timeout で見る）。1 I/O の command_lock は約 6〜7 回に。
- 5330（`w308-d`、27a02af59 ＋ 一時の計測の差分）:

| 負荷 | p001（main） | p002＋p003（w308-b） | 27a02af59（w308-d） | Debian |
| --- | --- | --- | --- | --- |
| direct QD32（thread） | 124620 IOPS | 143242 | 214465（878 MB/s） | 964 MB/s（91%） |
| direct QD32（process） | 124275 | 134575 | 215825（884 MB/s） | — |
| cached QD32（thread） | 139233 | 172477 | 204840（839 MB/s） | 964（87%） |
| direct・cached QD1 | 13386・14279 | 13540・14804 | 13619・15021 | — |

- direct QD32 の区間: 入口→post 6〜9 µs（pin 2〜5）、post→CQ 126 µs（device に出ている command が増えた）、CQ→waiter 5 µs、起床→戻り 3 µs、入口→戻り 142〜145 µs。cached QD32: 入口→post 29〜32 µs（pin→submit 12 µs＝buffer cache の道）、起床→戻り 15〜19 µs。
- QEMU（27a02af59 の serial の image）: boot-test、`qemu-nvme-shapes.sh`・`qemu-directio.sh`・`qemu-nvme-errors.sh` PASS。build は config-5330 だけ（他の 4 config は 84fd65ac6 で warning 0、27a02af59 は pci-nvme.c だけの変更で未 build）。

## q1652: 1 I/O の内訳と直し（2026-10-28、P1）

### Debian を同じ道具で測り直した

- `plan/ws308/tests/debian-qdstat.sh RANDREAD_LINUX [SECONDS]`（`SETTLE=1` で fstrim と 60 s の休み）: 5330 の Debian で randread（direct・cached、QD1・QD32、thread・process）を流し、前後の `/sys/block/nvme0n1/stat` から block 層の平均の時間（issue→完了）と平均の in flight、NVMe の割り込みの数を出す（remote 側は `qdstat-remote.sh`）。
- 今日の Debian（3 回、settle の有無で同じ）: direct QD32 224.8k〜225.5k IOPS（920〜923 MB/s）、block 層 138 µs・in flight 31.2 → Linux の host 側は 1 I/O 約 4 µs。QD1 13.4k〜13.6k（block 71 µs）。以前の基準 964 MB/s（235k）より低い（device の状態か Debian の版の差。原因は調べていない）。受け入れは同じ時間帯の Debian と比べる。

### 内訳（計測の kernel、commit しない）

- 計測の差分: `build/tmp/exp-io-4.patch`・`build/tmp/exp-io-4.h`（rdtsc の stamp、syscall の道に 16 点、IRQ の数・時間・CPU、HAL の syscall の出入り）、直しを足した `build/tmp/m3-full.patch`・`build/tmp/m3-exp-io.h`。timecounter の読み（大域の CAS）を stamp に使った最初の版（exp-io-3）は QD32 を 214k→175k に落とし、command_lock の保持を伸ばして内訳を歪めた（捨てた）。
- 5330（`p1-m2`、27a02af59 ＋ transfer_run の直し ＋ 計測）direct QD32 208k の 1 I/O（µs）: syscall の外 5.5、入口→pin 2.4、pin→submit 1.2、begin 0.6、begin→post（slot の取得・組み立て・post）2.0、device 127.3、IRQ→起床 1.5、起床→実行 3.7、command_lock の取り直し 0.7、→wait の戻り 0.3、→submit の戻り（release・end_bio・bio_complete）4.5、→handler の終わり 0.8、→user への戻り 1.5。host 側 計 約 25 µs（Debian 約 4 µs）。cached QD32: 入口→pin 5〜16（metadata の mutex の待ちが取りの 11〜14%）、copy_cached_lines の miss 2.6、transfer_run 0.3、reference_line 2.8、begin 2.9、post 5.8、submit の戻り 16〜20、handler の終わり 6〜7。
- `bio_complete` と `bio_admit` は大域の `disk_registry_lock`（割り込みを止める spin）の下で時計（timecounter、大域の CAS）を読んでいた。`transfer_run` は 4 KiB の 1 行の読みでも `disk_page_vector_supported`（disk_registry_lock）と `kern_malloc`/`kern_free` をしてから 1 行の道に戻っていた。NVMe の slot の取得は 63 slot の state を前から線形に読む（command_lock の下で、他の CPU が書く line）。

### 直し（a609f74a1）

- (a) `transfer_run`（buf.c）: 2 行に満たない範囲は、driver への問い合わせと heap の確保の前に 1 行の道へ（統計 `IO_BUF_SINGLE_GEOMETRY` は今と同じ）。
- (b) disk.c: hw.diskstats の時計を `disk_registry_lock` の前に読む（`disk_stats_stamp`）。他の CPU が間に入ると busy の区間の端がその分ずれるだけ（比較は古い時刻を許す）。
- (c) pci-nvme.c: 空き slot の hint（`io_slot_free_hint`、64 bit）。release・queue の初期化・作り直しで bit を立て、取得は hint の bit を下から見て state が FREE か確かめる。hint が空なら今どおり全 slot を見る（bit の漏れがあっても見つかる）。
- (d) pci-nvme.c: 遅延の計測（WS299 p004 の log）を command id の世代で 8 本に 1 本に。他の command は post で時計を読まず、完了で command_lock の下で時計を読まない。log は計測した 4096 本ごと。
- (e) pci-nvme.c: request の最後の command（それまでに失敗なし、recovery の持ち主でない、DMA の不確かさなし）は、slot の返却と `end_bio` を 1 回の command_lock で。caller の page に直に入った command（bounce なし）は、起床後に command_lock を取る 1 回の保持で状態の確認・返却・end_bio まで済ませる。1 I/O の command_lock は 6 回→4 回（begin・acquire・post・完了）。

### 試験

- build: config-5330（`build/p1-k1`）・pcat・visionfive2・rpi4・m68k-virt で rc 0・warning 0（27a02af59 の他の 4 config の確認を兼ねる）。build のため main の `build/amd64/sysroot`・`build/i386/sysroot` を worktree の build/ に複写した。
- QEMU（serial の image `build/p1-q1`、cache 128 MiB の image `build/p1-q2`、a609f74a1 と同じ source）: `qemu-nvme-shapes.sh` PASS（5 checksums equal）、`qemu-directio.sh` PASS、`qemu-nvme-errors.sh` PASS（失敗の run 3、良い run 6、guest 生存）、`qemu-readahead.sh` PASS（reload の拒否 0）、`qemu-grow-reboot.sh` PASS、`qemu-cache-stress.sh` PASS（7 checksums、file intact、reload busy 1/8）、`boot-test.sh` PASS（`build/tmp/q1-boot-test/login.png`）。
- 5330（同じ時間帯、`nvme-bench-5330.sh zedbsd p1-k1 build/p1-k1/vmunix qdprobe` → `debian-qdstat.sh`、固まりなし）:

| 負荷（4 KiB） | zedBSD a609f74a1 | Debian（直後） | 比 |
| --- | --- | --- | --- |
| direct QD32（thread） | 223827 IOPS（916.7 MB/s） | 225484（923.5） | 99.3% |
| direct QD32（process） | 224571 | 225370 | 99.6% |
| direct QD1 | 14005 | 13367 | 104.8% |
| cached QD32（thread） | 240396（984.6 MB/s） | — | 以前の基準 964 の 102% |
| cached QD1 | 15291 | — | — |

- 計測の kernel（`p1-m3`、直し ＋ 計測）direct QD32 221k の 1 I/O: submit の戻り 4.5→1.0、post 2.0→1.8、IRQ→起床 1.5→1.0、起床→実行 3.7→2.7 µs。syscall の外は 4.8 µs のままだが、HAL の syscall の出口→入口（user と asm）は 0.4 µs で、残り約 4.4 µs は user への戻りの stamp から HAL の出口までと HAL の入口から handler の入口までに出る（その間は割り込みを止めた数行で、時間がかかる理由が無い）。QD1 では同じ値が 255 µs と出て、計測の誤り（新しい thread の field の初期化の漏れなど）の疑い。未解明（数の目標には要らない）。
- 未実施: timeout・stop・detach の最中に slot に眠る者がいる場合、TSC の無い機械の起動、p006 の負荷の試験。

## 危険

- slot の waitq の condition lock の変更は、起こす全ての道（割り込み・fail_all・停止・recovery）が wait_lock を取る事に依る。漏れると waitq の list が壊れる。grep で `slot->waitq`・`io_slots[...].waitq` の全ての使い手を見る（今 :3237・3958・4099・4107・4986・6146）。
- timecounter: available の間に frequency が変わる道が無い事に依る（起動の後の再 publish が無い事を確かめる）。
