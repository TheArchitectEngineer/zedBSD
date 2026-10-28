<!-- awesome-plan project=zedbsd record=ws308-p002 -->

# ws308-p002 起床の道と NVMe の command_lock、amd64 の timecounter の大域の lock

- 親: [WS308](../ws.md)。Queue: q1650（担当 P1）。前提: [p001](../phase001/phase.md) の 5330 の区間の計測。
- Status: in-progress（2026-10-28 P1: 設計、design-reviewer へ）
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
- `frequency_hz` は publish の時に `available` の release の前に書かれ、available の間は変わらない（publish は available の時に断る、:329）。disable の後の publish は起動の時の経路だけ。lock の無い読み手は available を acquire で見てから frequency を読むので、available=1 の間の frequency は一つ。
- 割り込みを止める wrapper（timecounter.c:766）は残す（同じ CPU の割り込みとの順は今と同じ）。`state->lock` の field は残し（struct の配置を変えない）、init の 0 の store も残す。
- KVM の skew の clamp（REGRESS の log）は今と同じ。TSC の無い・不安定な機械は `available` が 0 のまま（今と同じ「no counter」）。

### D2. NVMe の割り込みが command_lock の外で起こす（pci-nvme.c）

- slot ごとに `wait_lock`（spinlock、rank `LOCK_RANK_DISK`＝command_lock の `LOCK_RANK_DEVICE` より後）を足し、`slot->waitq` の condition lock を command_lock から `wait_lock` に変える。順: command_lock → wait_lock（逆は取らない）。
- 起こす側: `nvme_io_slot_wake(slot)` = wait_lock の下で `waitq_wake_all(&slot->waitq)`。
  - 割り込み（`nvme_irq_io_locked`）: command_lock の下で state を CQ_DONE・FAULTED にし、slot を手元の配列（最大 `NVME_IO_MAX_SLOTS`）に積む。`nvme_irq` が command_lock を放した後に、積んだ slot を順に `nvme_io_slot_wake`。`io_state_waitq` の起床は command_lock の下に残す（condition lock は command_lock のまま、待つ者がいなければ数を足すだけ）が、1 pass に 1 回にまとめる。
  - 失敗の道（`nvme_io_fail_all_locked` :3940 付近・停止 :3230 付近）: command_lock を持ったまま `nvme_io_slot_wake`（順を守る）。
- 待つ側（`nvme_io_wait_completion`）: command_lock の下で state が ACTIVE なら、眠れる thread は command_lock を放し、wait_lock の下で「state が ACTIVE の間、waitq の sequence を取り `waitq_sleep(&slot->waitq, &slot->wait_lock, …, deadline)`」を回し、抜けたら command_lock を取り直して今と同じ後処理（ETIMEDOUT で ACTIVE なら fail_all、latency の計測、COPYING へ、recovery の owner）。state の読みは `__atomic_load_n`（書きは command_lock の下、起床はその後に wait_lock の下なので、wait_lock の下の読みが ACTIVE を見て眠れば必ず起こされる）。idle の thread（起動の早い時期）は今の poll の道（`nvme_io_wait_locked` の sched_yield、waitq を触らない）のまま。
- 正しさ: slot の waitq に眠るのは、その slot を持つ 1 本の waiter だけ（:4986 だけが眠る）。起こす側が wait_lock を取る時点で waiter が既に slot を release・再取得していても、新しい waiter は state が ACTIVE なら眠り直す（偽の起床は loop で吸収）。slot の memory は controller の配列で、detach まで残る。

### D3. command id と slot の対応を O(1) に（pci-nvme.c、純関数は include/drivers/pci/pci-nvme-protocol.h）

- command id = `(世代 << 6) | (slot の番号 + 1)`（番号は 0〜62、`NVME_IO_MAX_SLOTS` 63）。slot ごとの 10 bit の世代を取得ごとに進める。0 にならず、出ている command の間で一意（番号が一意）。
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

## 危険

- slot の waitq の condition lock の変更は、起こす全ての道（割り込み・fail_all・停止・recovery）が wait_lock を取る事に依る。漏れると waitq の list が壊れる。grep で `slot->waitq`・`io_slots[...].waitq` の全ての使い手を見る（今 :3237・3958・4099・4107・4986・6146）。
- timecounter: available の間に frequency が変わる道が無い事に依る（起動の後の再 publish が無い事を確かめる）。
