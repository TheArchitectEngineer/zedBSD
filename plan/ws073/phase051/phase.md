<!-- awesome-plan project=zedbsd record=ws073-p051 -->
# ws073-p051: BUG-135 — UFS の journal の commit の flush を mount の lock の外へ出し、stat の秒単位の停止の残りを直す

Status: in-progress（q653-i01、P9 generation1 → 2026-10-04 再開 P2 generation8。実装 e5631f9 → c79e089 → hold の range の直し 89ae1fa（案 3、Q1 の決定）。amd64・pcat・rpi4 の build warning 0、P2 の QEMU で crash-test 9・window・fsprobe の churn が PASS。統合・判定は Q1。T1・T2 の試験は user の指示で除外）
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
- `buf_unpin(..., pin, &kept)` は他の tag が残った line の数を返し、N の finish はそれが 0 でない logged range を `j3_home_direct` で
  slot の copy から home へ direct に書く（次の節）。

### 閉じた transaction の range と line の粒度の穴（Q1 のレビューで見つけ、c79e089 で直した）

payload は range（fragment、1 KiB = 2 sector）の単位で写し、pin と cache の書きは line（4 KiB）の単位。N が line L の fragment A を、
running の N＋1 が同じ L の別の fragment B を書くと、L の tag は N＋1 になり、N の finish は L を unpin できず cache も L を書かない
（pinned）。N＋1 の payload には B しか無いので、N＋1 の record が durable になった後・L を home へ書く前に電源が切れると、replay は
最新の N＋1 だけを適用し A はどこにも無い（旧 code は commit が同期・排他で、N の pin が全て外れた後にしか N＋1 が pin できず、N＋1 の
commit の最初の `buf_sync` が L を書いていたので無かった穴。e5631f9 で入り、c79e089 で直した）。

直し: N の finish は range ごとに `buf_unpin(..., pin, &kept)`（他の tag が残る line の数）を見て、kept == 0 なら cache で書き
（`buf_writeback_range`）、kept != 0 なら `j3_home_direct` が N の slot にある A の copy（descriptor の後、logged range の順）を読み、
`backing_mutation_begin_disk_filesystem` ＋ `disk_write_direct_context`（`writeback_line_whole` と同じ経路、cache を通らない）で A の
home sector だけに書く。cache の L（B を含む新しい内容）はそのまま残り、N＋1 の commit の後に whole line で書かれる。順序は
A の direct write → N＋1 の durable 1（flush）→ N＋1 の record → L の whole write。

残課題: hold の range（running が解放した block への content、payload に copy が無い）の line が pin し直された場合は cache の書きを
待つしかない。content は journal の対象の外で、fsync が返した後にその content が disk に無い窓は、fsync の最後の `disk_sync` を lock の
外へ出した p045 以降と同じ（別の transaction が同じ line を pin し直すと `disk_sync` が飛ばす）。頻度は低い（同じ ~1 秒の間に解放した
block へ content を書き、かつ同じ 4 KiB line を別の transaction の metadata か hold が使う）。直すなら hold の content も slot に写すか、
fsync が自分の pin した line の unpin を待つ形にする。

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

### 穴の直し c79e089 の後の確認（2026-10-04 01:00〜、ラップアップで途中まで）

- build: `BUILD=build/p9-amd64` vmunix exit 0・warning 0・check PASS、DWARF の `build/p9-dbg` も exit 0・warning 0。style-diff（base 9e228b6）
  変更行の違反 0、`git diff --check` OK。image: `build/p9-logs/fix2.img`（c79e089 の kernel を ESP に差し替え）、`dbg2.img`（DWARF）、
  `dbg-e56.img`（e5631f9 の DWARF kernel、穴の比較用）。
- `crash-test.sh build/p9-logs/fix2.img 4 9`: 4 秒 HOLDS 2565 = PREFIX 2565、9 秒 HOLDS 3182 = PREFIX 3182、`UFS OK`（PASS）。
- `root-crash.sh build/p9-logs/fix2.img 6`: `durable-content`・`churn 50`・root `UFS OK`（PASS）。
- fsprobe の churn（c79e089 の kernel）: **未実施**（走り始めた所で Q1 のラップアップの指示、止めた）。e5631f9 の kernel の結果（上の表）は
  c79e089 でも変わらないと見る（直しは finish の home の書き方だけで、lock の持ち方は同じ）が、観測していない。
- 穴を突く crash の試験 [tests/p051-window.sh](../tests/p051-window.sh)（新。gdbstub で `j3_home_direct` が走る＝穴の条件が起きた
  transaction X の finish を待ち、次の `j3_commit_seal` が返った直後＝X＋1 の record が durable で home が未書きの所で QEMU を kill、
  再起動して replay、crash-grow の check と host の check-volume）: **未実施**（書いただけ、syntax check のみ。QEMU の kill は host の
  page cache に届いた write を失わないので「flush の前の電源断」は模せないが、この穴は A が device にまったく書かれない種類なので
  kill で突ける）。e5631f9 の kernel には `j3_home_direct` が無いので `ARM=j3_finish_commit SEALS=2` で流して比べる案。

### 未実施

- T1・T2 への試験の依頼（user の指示で除外）。受け入れの QEMU の試験（UFS の試験の一式・boot test）: **未実施（user の指示で除外）**。
  上の QEMU の結果は P9 自身の診断。実機: 未実施。
- BUG-143（IME の確定）・BUG-147（app の起動）・sshd の stall の症状での確認（p045 の残り）: 未実施。
- i386・arm64 の build: 未実施（変更は arch に依らない C）。

### 残り・再開点（2026-10-04 ラップアップ、P9 は終了）

- commit: e5631f9（最初の実装、range/line の粒度の穴あり。**単独では統合しない**）→ 4ed7e93（記録）→ c79e089（穴の直し: `buf_unpin` の
  `kept`、`j3_home_direct`、`closed_staging`）→ 記録の commit。統合は c79e089 以降を含めて、user の確認の後に Q1 が行う（Q1 の決め）。
- 再開の条件（次の担当が c79e089 の kernel で）: (a) `tests/p051-window.sh build/<dbg>/hdd-image.img build/<dbg>/vmunix OUT` を直した
  DWARF kernel で数回流し PASS を見る（`ARM=j3_finish_commit SEALS=2` で e5631f9 の kernel と比べる）。(b) fsprobe の churn を
  `p051-experiment.sh` で c79e089 の kernel で 1 回（1 秒超 0 を確かめる）。(c) 残課題の hold の range（上の「穴」の節）の扱いを決める。
  (d) i386・arm64 の build。これらの後に Q1 が判定し、T1・T2 の試験は user の判断。
- hold の range の残課題: content の line が pin し直されたときの fsync の窓（p045 以降と同じ、頻度は低い）。直し方の案は上の節。
- 残る 100〜200 ms の stat の遅れは秒単位ではない。追うなら `p051-experiment.sh IMAGE OUT FSPROBE VMUNIX_DWARF` の標本の間隔を狭めるか、
  stat の slow の行の時刻と標本の時刻を照合する。
- 使い捨ての build: `build/p9-amd64`（image）、`build/p9-base`（直す前の kernel）、`build/p9-dbg`（DWARF）、`build/p9-logs`（image の複製
  base.img・dbg.img 各 2.2 GB、fsprobe、run-*/ の記録）。全て worktree の build で、消してよい。

## 再開（2026-10-04、P2 generation8、Q1 の指示）

Q1 の指示: 再開の条件のうち (d) i386・arm64 の build、(c) hold の range の扱いの読み、(a) `p051-window.sh` を自分の QEMU 1 つで短く。
T1・T2 には依頼しない（user の指示）。kernel は main（c79e089 を含む）の source のまま、kernel の C は変えていない。

### (d) i386・arm64 の build

pcat（i386）・rpi4（arm64）・pc98（i386）・intelmac（amd64）の kernel を build: 4 つとも exit 0・warning 0。pcat と rpi4 の binary に
`j3_finish_commit`・`j3_home_direct` があることを symbol で確かめた。実行は未実施（QEMU・実機とも）。

### (a) 穴を突く crash の試験（P2 の QEMU 1 つ、短く）

DWARF の kernel の SSH の image: `flock /tmp/zedbsd-image-build.lock plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-ssh.mk
build/p2-b135 "ZEDBSD_KERNEL_LTO_CFLAGS=-g -fno-omit-frame-pointer"`（exit 0、`.debug_info` あり）。実行は
`GUEST_RUNTIME=build/p2-b135-run sh plan/ws073/tests/p051-window.sh build/p2-b135/hdd-image.img build/p2-b135/vmunix OUT [ARM SEALS]`。

**試験の道具の直し（kernel ではない）**: 最初の 3 回は全て `FAIL the window was not reached`。gdb.txt を読むと、
1. gdb 16.3（Debian）の batch は `finish` の `Run till exit` を出さず、到達の判定の grep が常に外れる。
2. `j3_commit_seal` の breakpoint に mount の条件が無く、root の commit も止める: 1 回目は arm が `mounts+5936`（/jour）で seal が
   `mounts+848`（root）、2・3 回目は逆。窓（同じ mount の X の finish の後の X＋1 の seal）を突いていなかった。

直し（`tests/p051-window.sh`）: arm は `if $_streq(mountp->m_path, "/jour")`、その `mountp` を `$m` に取り、`break j3_commit_seal if
mountp == $m`。到達の判定は `rip` が `<j3_…>`（seal の呼び手）に戻っていることと `Inferior 1 … killed`。

直した後の結果（全て mount は /jour、`$1 = "/jour"`、seal は `sequence=5`＝`j3_home_direct` が走った sequence 4 の次）:

| 回 | ARM SEALS | 結果 |
| --- | --- | --- |
| window1 | j3_home_direct 1 | PASS: HOLDS 2942 = PREFIX 2942、volume・root `UFS OK`。rip `<j3_finish_commit+213>` |
| window2 | j3_home_direct 1 | 判定外（`FAIL the window was not reached`）: `finish` の途中で別の vCPU の thread に SIGTRAP（`asm_get_rflags`）が来て gdb が止まり、seal の中で kill。判定が正しく弾いた。kernel の不具合の証拠ではない |
| window3 | j3_home_direct 1 | PASS: names 3001、volume・root `UFS OK` |
| window-finish | j3_finish_commit 2 | PASS: names 2902、volume・root `UFS OK` |

QEMU の kill は host の page cache に届いた write を失わないので、「flush の前の電源断」は模せない（P9 の記録のとおり）。
`j3_home_direct` の穴（A が device にまったく書かれない種類）は kill で突ける。e5631f9 の kernel との比較は未実施
（e5631f9 の DWARF の image を作っていない）。(b) fsprobe の churn: 未実施。

### (c) hold の range の扱い（code の読み、決めは Q1）

読んだ所: `j3_write`（content が running か閉じた側の freed set の block に落ちると hold＝`buf_write_pinned` で pin、payload に copy 無し、
`j3_room` は hold を payload に数えない）、`j3_finish_commit`（hold の range は `buf_unpin(..., closed_sequence, &kept)` の後、kept == 0 なら
`buf_writeback_range`、kept != 0 なら何もしない）、`ufs_sync`（close → finish → `disk_sync`、`disk_sync` の `buf_sync` は pinned な line を飛ばす）。

窓: 閉じた X の hold の range R の 4 KiB line を、X の finish の前に running の X＋1 が pin し直す（X＋1 の metadata の fragment か、X が解放した
block への X＋1 の hold）。X の finish は R を書かず、fsync の `disk_sync` も飛ばす。X＋1 の commit（次の interval、~1 秒、か次の fsync）が
unpin して `buf_writeback_range` が line を whole で書くまで、R は cache にしか無い。その間の電源断では、X の commit は durable（R の block は
新しい持ち主のもの）で、R の content は無く、その block は前の持ち主の古い内容のまま。fsync が返った content が無く、別の file だった内容が
見える。

- 新しい窓ではない: hold の content は commit の後に cache で書く設計で、seal から home の書きまでの間の電源断は前から同じ結果
  （replay は hold を書かない）。re-pin はその間を X＋1 の commit まで延ばし、fsync の後にも残す。後者は p045 で `disk_sync` を lock の外へ
  出してからの窓と同じ種類（普通の content でも、line を他の transaction が pin し直すと `disk_sync` が飛ばす）。
- 頻度: fragment（1 KiB）単位の割り当ての所（小さい file の末尾・小さい directory）で、同じ ~1 秒に解放と再利用が起き、同じ 4 KiB line を
  別の transaction が使うとき。

案:

1. **hold の content も slot に写す**（logged にして `j3_home_direct` で書く）: 却下を勧める。replay は最新の commit を毎回当てるので、
   commit の後に journal の外で書かれ fsync された新しい content を、replay が古い copy で上書きしうる（metadata は後の変更が pin されるので
   起きないが、content は pin されない）。payload も content の量だけ減る。
2. **kept の hold の sector だけを cache から direct に書く**: 却下を勧める。その sector は X＋1 が同じ block を解放して再び割り当てた後の
   content（X＋1 の hold）でありうる。X＋1 の commit の前に home へ出すと、X の状態の持ち主に別の file の content が見える。freed set を
   見て分ければ避けられるが、lock と競合の扱いが増える。
3. **fsync が自分の hold を待つ**（勧める）: `j3_finish_commit` が kept != 0 で書かなかった hold の range の数を返し、`ufs_sync` はそれが
   0 でないときだけ close → finish をもう 1 回（上限 2 回）行ってから `disk_sync` する。X＋1 の finish が line を unpin して whole で書く。
   追加の commit（flush 2 回）はこの稀な場合だけで、普段の fsync の遅さ（BUG-135 の対象）は変わらない。X＋2 がまた pin し直すほどの
   churn では上限で諦め、今と同じ窓が残る（記録する）。flusher（`j3_hook`）の側は fsync の約束が無いので変えない。
4. **直さない**（記録だけ）: p045 で受け入れた普通の content の窓と同じ種類、頻度が低い。

推奨: 3 を別の Phase（または p051 の追加の範囲）で小さく実装する。普通の content が pin し直された line の窓（p045 以降）は 3 でも
直らない（fsync はどの line が自分の file のものかを知らない）。直すなら別の bug として扱う。決めは Q1（user の判断が要るなら Q1 から）。

### 未実施（この再開）

- e5631f9 の kernel との window の比較、(b) fsprobe の churn、BUG-143・BUG-147・sshd の症状での確認、i386・arm64 の実行、受け入れの
  QEMU の試験（user の指示で T に依頼しない）、実機。
- 使い捨て: `build/p2-b135`（DWARF の image）、`build/p2-b135-run`。worktree の build で、消してよい。

## hold の range の直し（案 3、2026-10-04、P2 generation8。Q1 の決定「案 3 で直して」、技術の選択として委任の範囲）

### 実装（`src/drivers/fs/ufs.c`、89ae1fa）

- `struct ufs_j3` に `home_owed`（journal の lock の下）。finish が hold の range を、後の transaction が line を pin し直していた
  （`buf_unpin` の kept != 0）ために書かなかったら 1 にする。どの range の line も後の transaction に残らない（kept が全て 0）finish で 0 に戻す。
  0 に戻せる理由: owed の line を pin し直した transaction はその line に自分の range を持つので、その finish の kept が 0 なら
  `buf_writeback_range` が line を whole で書いている（owed の hold の sector を含む）。kept が残る finish では owed のまま（保守的）。
- Q1 の案は「`j3_finish_commit` が数を返す」だったが、状態に置く形にした。理由: flusher の `j3_hook` の finish と `j3_write` の
  `j3_commit_now` の finish が、fsync の知らない所で hold を飛ばしうる。fsync が自分の finish の戻り値だけを見ると、それを見落とす。
- `ufs_sync` は 1 回の commit（新しい `ufs_sync_commit`: 従来の close の loop（commit 中なら lock を離して待つ）＋ finish）の後、
  `home_owed` が 1 の間だけ commit をあと最大 `UFS_SYNC_OWED_ROUNDS`（2）回繰り返し、それから `disk_sync` する。owed でない普段の
  fsync の手順と flush の回数は変わらない（BUG-135 の lock の外の flush もそのまま）。

### fsync の保証（この直しの後）

`fsync`（`ufs_file_sync` → `ufs_sync`）が 0 を返した後の電源断で:

- その時までに閉じた transaction（fsync 自身が閉じた X を含む）の metadata は durable（従来どおり。commit record が durable）。
- X までの hold の content（X か前の transaction が解放した block への content）も、後の transaction がその line を pin し直していても、
  owed の追加の commit（最大 2 回）でその line の次の持ち主が commit して line を whole で書き、`disk_sync` の flush で durable になる。
- 例外（記録）: 追加の 2 回の間にも毎回新しい transaction が同じ line を pin し直し続けると、上限で諦め、その hold の content は
  次の commit まで cache にしか無い（直す前と同じ窓。fragment を共有する line への 1 秒あたり 3 回以上の連続した再 pin が要る）。
- 対象の外（下の bug の案）: hold でない普通の content の line が pin し直された場合。

### 確かめ

- kernel の build（`make -s ZEDBSD_CONFIG=config/ci/config-<p>.mk BUILD=build/p2-<p> build/p2-<p>/vmunix`）: amd64・pcat・rpi4 とも
  exit 0、warning 0、error 0。
- style: `plan/tools/style-check.py` と P2 の補助の checker で、HEAD の ufs.c と比べて新しい指摘 0。
- DWARF の SSH の image（`build/p2-b135`、上と同じ作り方、89ae1fa の kernel）で P2 の QEMU 1 つ（T には依頼していない、user の指示）:
  - `bash plan/tools/ufs/crash-test.sh build/p2-b135/hdd-image.img 9`: HOLDS 3182 = PREFIX 3182、`UFS OK`（PASS）。
  - `sh plan/ws073/tests/p051-window.sh build/p2-b135/hdd-image.img build/p2-b135/vmunix build/p2-b135/window1`: `$1 = "/jour"`、
    seal は sequence 5、HOLDS 3065 = PREFIX 3065、volume・root `UFS OK`（PASS）。
  - fsprobe の churn（`CHURN=1 LOAD_JOBS=2 RUN_SECONDS=45 sh plan/ws073/tests/p051-experiment.sh … build/p2-b135/fsprobe`、
    fsprobe は `fsprobe-build.sh` で build）: **stat 4500 回、最長 4 ms、100 ms 超 0、1 秒超 0**。nap 最長 11 ms。fsync の書き手は
    190 回まで（打ち切り、設計どおり）、100 ms 超の 139 回のうち 1 秒超 23 回（host の dd の負荷の下の device の flush。fsync 自身の
    遅さは BUG-135 の対象外で、直す前の fsync の数は記録に無いので比較していない）。
- owed の追加の commit が実際に走った回数は観測していない（数える log を足していない）。hold の再 pin の窓を電源断で突く試験は無い
  （QEMU の kill は host の page cache の write を失わず、窓が cache にだけある content を突けない）。

## 別の bug の ticket の案（Q1 が Bug にする。p045 以降の普通の content の再 pin の窓）

- 題: UFS の fsync が返った後、普通の content が durable でないことがある（line を journal の後の transaction が pin し直したとき）。
- 期待: `fsync(fd)` が 0 を返したら、その file の content は電源断の後も残る。
- 観測（code の読みだけ、再現は未）: content は journal の外の delayed write で、`ufs_sync` の最後の `disk_sync` → `buf_sync` が書く。
  `buf_sync` は pinned な line を飛ばす。p045（`disk_sync` を lock の外へ）以降、fsync の close と `disk_sync` の間に別の thread の変更が
  running の transaction でその 4 KiB line を pin する（同じ line の fragment に metadata（directory の block・indirect・inode の block）か
  hold がある）と、fsync の content は次の commit（~1 秒）まで cache にしか無い。
- 影響: fragment（1 KiB）を共有する line（小さい file の末尾と小さい directory）。窓は ~1 秒、電源断がそこに当たる必要がある。
  metadata の整合は保たれる（content だけが古い/0）。
- 発見: ws073-p051（BUG-135）の hold の range の読み（2026-10-04、P2）。p045 の記録の「fsync の最後の disk_sync を lock の外へ」と関係。
- 直し方の候補: (1) fsync が file の dirty な line を知って（inode の範囲を `buf_sync` に渡す）、pinned なら次の commit を待つ。
  (2) content の line を metadata の line と共有しないよう割り当てる（format・allocator の変更、大きい）。(3) `buf_sync` が pinned な
  line の中の pin されていない sector を direct に書く（sector の単位の dirty の管理が要る）。
- 再現の案: fragment の割り当ての所で小さい file の追記＋fsync と、同じ line の directory の変更を並べ、fsync の直後に gdbstub で
  止めて `buf` の line の dirty と pin を読む（kill では窓を突けない）。
