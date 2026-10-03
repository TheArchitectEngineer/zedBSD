<!-- awesome-plan project=zedbsd record=ws073-p051 -->
# ws073-p051: BUG-135 — UFS の journal の commit の flush を mount の lock の外へ出し、stat の秒単位の停止の残りを直す

Status: in-progress（q653-i01、P9 generation1（Fable 5.1、high）、2026-10-04。実装・build・P9 自身の QEMU の診断と crash test まで済み、Q1 の merge と判定待ち。T1・T2 の試験は user の指示で除外）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-135](../../bugs/BUG-135.md)
Queue: q653（2026-10-04 user「BUG-135はFable 5.1 HighのサブエージェントP9に修正を依頼しましょう。テストは依頼しないで除外しましょう。」）

## 範囲（Q1、2026-10-04）

[ws073-p045](../phase045/phase.md)（q624）が直した後に残る stat の停止（4 回のうち 2 回、最長 3.8 秒）の原因を特定して直す。
確認は build（warning 0）・host 試験・自分の診断の範囲。**T1・T2 への QEMU・実機の試験の依頼はしない（user の指示）**。
HAL の API は変えない。

## 原因（code の読みと p045 の gdbstub の記録）

p045 の後の鎖:

1. `stat` → `ufs_lookup` → `namespace_share` が `namespace_lock` を一瞬通る。名前の変更（`ufs_rename`・`ufs_create` など）が
   `namespace_enter` で `namespace_lock` を排他で持っている間、lookup はそこで待つ。
2. 名前の変更は `namespace_lock` を持ったまま `ms->lock`（mount の lock）を待つ。
3. `ms->lock` は fsync（`ufs_file_sync` → `ufs_sync`）か flusher の `j3_hook` が持ったまま `j3_commit_locked` を走らせる。
   `j3_commit_locked` は payload の複写の後に `j3_commit_seal` で **descriptor を書いて `disk_sync`（buf_sync ＋ `bio_flush`）、
   commit record を書いてもう一度 `disk_sync`** する。host の disk の flush が秒単位のとき、この 2 回の flush の間 `ms->lock` が
   取られたままで、鎖の先の lookup が秒単位で止まる。

p045 の直し（lookup と commit が namespace を共有、fsync の最後の `disk_sync` を lock の外へ）は hook の commit を lookup から外したが、
名前の変更 → `ms->lock` → commit の flush の鎖は残っていた（p045 の記録の「残る停止」と同じ）。

## 直し（設計）

journal（j3）の commit を 2 段に分ける:

- **閉じる段**（`j3_close_transaction`。`ms->lock` と `j3.lock` の中。memory の複写だけ）: pinned な metadata の今の内容を journal の slot
  の cache line へ写し（`j3_commit_payload`、従来どおり）、descriptor を作り、running transaction の ranges・freed set・sequence を
  「閉じた transaction」の側（`closed_*`）へ入れ替え、新しい running transaction を空で始める（sequence＋1）。
- **書いて flush する段**（`j3_finish_commit`。lock の外）: descriptor を書く → durable → commit record を書く → durable
  （`j3_commit_seal`、順序は従来どおり）→ 閉じた transaction の ranges の pin を外す → その home を書く（`buf_writeback_range`）→
  `j3.lock` の中で閉じた側を空にし `closing` を 0 にして待ち手を起こす。
- 同時に走る commit は 1 つ（`closing`）。fsync（`ufs_sync`）は `ms->lock` と `j3.lock` を取って `closing` なら両方を離して
  `mutex_wait` で待ってやり直す（`ms->lock` を持ったまま flush を待たない）。`j3_hook` は commit が書かれている間はその回を飛ばす
  （次の interval で commit する）。transaction が満ちたときの commit（`j3_write`）は従来どおり lock の中で同期的に行う（稀）。
- **pin を transaction の sequence の tag に**（`include/kern/buf.h`・`src/kern/buf.c`）: `b_journal_pin` は 0 か pin した transaction の
  sequence。`buf_write_pinned(..., pin)` は tag を上書きし、`buf_unpin(..., pin)` は tag が一致する line だけ外す。閉じた transaction N の
  commit が書かれている間に running の N＋1 が同じ line を書き換えると tag は N＋1 になり、N の unpin はその line を外さない
  （N＋1 の変更が N＋1 の commit record より先に home へ届かない）。
- **freed set を 2 つ**: 閉じた transaction が解放した block は、その commit が durable になるまで content の書き込みを hold する
  （`j3_content_freed` は running と閉じた側の両方を見る）。
- 新しい `buf_writeback_range(disk, block, count)`: 範囲の line を home へ書く（pinned な line は飛ばす）。N の home を N＋1 を閉じる前に
  device へ出すために使う（N＋1 が N の line を pin し直すと N＋2 の durable の `buf_sync` がその line を飛ばすため）。

### journal の順序の保証（変えない）

- 「content と前の home を書く → durable → descriptor → durable → commit record → durable → home の書き」は保たれる。durable は
  書いて flush する段の `j3_commit_seal` の中で従来どおり 2 回。閉じた transaction の pinned な line は commit record が durable に
  なる（`j3_commit_seal` が返る）まで `buf_unpin` されず、flusher・`buf_sync` は pinned な line を飛ばす（`writeback_line`）。
- N の home が device へ出る前に N の slot の反対側（N−1 の slot）が上書きされることはない: N＋1 の閉じる段は N の finish
  （unpin と home の書き）の後で、N＋1 の record より前に N＋1 の durable が flush する。

## 実施（q653-i01、P9、2026-10-04 00:20〜）

### 変更

- `src/drivers/fs/ufs.c`: `struct ufs_j3` に閉じた transaction の側（`closing`・`commit_idle`・`closed_sequence`・`closed_ranges`・
  `closed_count`・`closed_freed`・`closed_freed_count`・`closed_descriptor`・`closed_desc_sectors`）。`j3_commit_locked`・`j3_commit`・
  `j3_unpin_all` を `j3_close_transaction`・`j3_finish_commit`・`j3_commit_wait`・`j3_commit_now`・`j3_unpin_ranges` に。
  `j3_commit_seal` は sequence を引数に。`j3_freed_test` は set を引数に、`j3_content_freed` は 2 つの set を見る。`j3_room`・
  `j3_commit_pending`。`j3_write` の満ちた時の commit は `j3_commit_now` の loop（失敗した seal のやり直し → running）。`ufs_sync` は
  `ufs_sync_close`（lock の中の部分）と `j3_finish_commit`（外）に分け、commit 中は `ms->lock` を離して `j3_commit_wait`。`j3_hook` は
  commit 中はその回を飛ばす（lock の前の `buf_sync` は不要になったので外した）。`j3_open` で閉じた側の table を取り、`j3_close` は
  commit 中を待ってから pending を全て commit（または pin を外す）し、閉じた側も返す。mount で `commit_idle` を init。
- `include/kern/buf.h`・`src/kern/buf.c`: `b_journal_pin` を `uint64_t` の tag に。`buf_write_pinned(..., pin)`（0 は EINVAL）、
  `buf_unpin(..., pin)`（tag が同じ line だけ外す）、新 `buf_writeback_range`（範囲の dirty な line を書く、pinned は飛ばす、flush しない）。
  `write_lines` の pin 引数は tag。
- 試験の道具: [tests/p051-experiment.sh](../tests/p051-experiment.sh)（guest で fsprobe の stat・nap・fsync・replace、host で dd の負荷
  `LOAD_JOBS` 本、`CHURN=1` で rename の loop と 2 本目の fsync の書き手、`VMUNIX_DWARF` で p045-locks.py の標本）。

### build・style（host）

- `make -j16 ZEDBSD_CONFIG=plan/tools/guest/config-amd64-ssh.mk BUILD=build/p9-amd64 vmunix`: exit 0、warning 0、`amd64 vmunix check: PASS`。
- 調査用 `BUILD=build/p9-dbg ZEDBSD_KERNEL_LTO_CFLAGS="-g -fno-omit-frame-pointer"`: exit 0、warning 0。
- `python3 plan/ws073/tests/style-diff.py src/drivers/fs/ufs.c src/kern/buf.c include/kern/buf.h`: 変更行の違反 0。`git diff --check` OK。
- SSH guest image `plan/tools/guest/build-ssh-image.sh build/p9-amd64`（flock の下）: exit 0、`check-amd64-native-image: OK`。
  直す前の比較用 kernel: 3 file を 9e228b6 に戻して `BUILD=build/p9-base` で build（exit 0）、`tests/kernel-image.sh` で同じ image の
  ESP の /vmunix だけ差し替え（`build/p9-logs/base.img`、DWARF の `dbg.img` も同様）。

### 自分の診断（QEMU、KVM、NVMe の root。user の指示により T1・T2 には依頼していない。実機は未実施）

`p051-experiment.sh`、45 秒、stat は 10 ms ごと 4500 回（cache に載った `/root/desktop.conf`）、nap 同じ、fsync・replace の書き手。
host は `dd bs=1M count=1000 conv=fdatasync` の loop（/proc/pressure/io の some avg60 34〜53%）。

| 条件 | kernel | stat の最長 | 100 ms 超 | 1 秒超 | nap の 100 ms 超 |
| --- | --- | --- | --- | --- | --- |
| 負荷 1 本 | 直す前（9e228b6） | 202 ms | 1 | 0 | 0 |
| 負荷 1 本 | 直した後 | 1 ms | 0 | 0 | 0 |
| 負荷 2 本 | 直す前 | 0 ms | 0 | 0 | 0 |
| 負荷 2 本 | 直した後 | 1 ms | 0 | 0 | 0 |
| 負荷 2 本 ＋ rename の churn ＋ fsync 2 本 | 直す前 | **2.11 秒**（p99 583 ms） | 202 | 7 | 0 |
| 負荷 2 本 ＋ rename の churn ＋ fsync 2 本 | 直した後 | 199 ms | 1 | 0 | 0 |

rename の churn（`mv r1 r2; mv r2 r1` の loop）は、名前の変更が `namespace_lock` を持ったまま `ms->lock` を待つ鎖を頻繁に起こす。
直す前はその間 stat が秒単位で止まり（fsync の commit の flush の時間）、直した後は commit の flush が `ms->lock` の外なので止まらない。
nap は常に 0 で、guest 全体の停止ではない。

- gdbstub の標本（DWARF の直した kernel `build/p9-dbg`、同じ churn の条件、0.5 秒ごとに p045-locks.py で mount の mutex の持ち主を
  52 回）: stat の最長 145 ms（100 ms 超 1 回、1 秒超 0）。52 回のうち lock が取られていたのは 4 回だけで、3 回は `namespace_lock` を
  走っている `mv`（rename 自体）、1 回は `ms->lock` を走っている `mv` が持っていた。commit や fsync が lock を持って flush を待つ標本は
  無い（直す前の p045 の標本では常にその形だった）。残る 100〜200 ms の 1 回は標本に掛からず正体は未確定（rename 自身の disk の読み・
  cache の追い出しの類と見る。秒単位ではなく、この bug の症状の外）。記録: `build/p9-logs/run-dbg1/locks.txt`（worktree の build、消える）。

### journal の整合（QEMU。Q1 の許可 2026-10-04「自分の診断の QEMU 1 つの中で crash-test.sh を流すのは範囲内」）

- `plan/tools/ufs/crash-test.sh build/p9-amd64/hdd-image.img 4 9`（直した kernel、v3 journal の作業 volume を NVMe の 2 台目に、名前の作成の
  途中で QEMU を止めて再起動・replay）: 4 秒 HOLDS 2875 = PREFIX 2875、9 秒 HOLDS 3182 = PREFIX 3182、どちらも `UFS OK`（PASS）。
- `plan/tools/ufs/root-crash.sh build/p9-amd64/hdd-image.img 6`（直した kernel、root を変更の途中で止めて replay）: `durable-content`
  が残り、`churn 50`、root の partition `UFS OK`（PASS）。
- 比較: `crash-test.sh build/p9-logs/base.img 9`（直す前の kernel）: HOLDS 3182 = PREFIX 3182、`UFS OK`。直す前後で結果は同じ。
- 電源断を模した replay の host だけの試験は無い（ufs.c の j3 を host で動かす harness が無く、zedimage-host は別実装、check-volume.py は
  journal を読まない）。上の QEMU の試験が代わり。

### 未実施

- T1・T2 への試験の依頼（user の指示で除外）。受け入れの QEMU の試験（UFS の試験の一式・boot test）: **未実施（user の指示で除外）**。
  上の QEMU の結果は P9 自身の診断。実機: 未実施。
- BUG-143（IME の確定）・BUG-147（app の起動）・sshd の stall の症状での確認（p045 の残り）: 未実施。
- i386・arm64 の build: 未実施（変更は arch に依らない C）。

### 残り・再開点

- Q1 が差分を読み、merge と Phase の判定をする（commit e5631f9 ＋ 記録の commit）。
- 残る 100〜200 ms の stat の遅れは秒単位ではない。追うなら `p051-experiment.sh IMAGE OUT FSPROBE VMUNIX_DWARF` の標本の間隔を狭めるか、
  stat の slow の行の時刻と標本の時刻を照合する。
- 使い捨ての build: `build/p9-amd64`（image）、`build/p9-base`（直す前の kernel）、`build/p9-dbg`（DWARF）、`build/p9-logs`（image の複製
  base.img・dbg.img 各 2.2 GB、fsprobe、run-*/ の記録）。全て worktree の build で、消してよい。
