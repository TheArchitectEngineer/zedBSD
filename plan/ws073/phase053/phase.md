<!-- awesome-plan project=zedbsd record=ws073-p053 -->
# ws073-p053: BUG-163 — UFS の fsync の後、journal の pin が同じ line の普通の content を止めて disk に届かない

Status: cleared（q664、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-163](../../bugs/BUG-163.md)
Queue: q664（Q1 2026-10-04「候補 (1) から設計して実装、自分の QEMU で gdbstub の再現と確認（BUG-135 と同じく T に依頼しない形で）」。
BUG-135 の試験を T に依頼しない user の指示 2026-10-04 の続き）

## 範囲

[BUG-163](../../bugs/BUG-163.md): `fsync(fd)` が 0 を返した後の電源断で、その file の content が disk に無いことがある（journal（j3）の後の
transaction が同じ 4 KiB の cache line を pin し直すと、`disk_sync` の `buf_sync` がその line を飛ばす）。設計・実装・自分の QEMU の
gdbstub で再現と確認。HAL は変えない。

## 分かったこと（code の読みと再現）

- zedBSD の UFS は data を **8 KiB の block 単位**でだけ割り当てる（`allocate_block_compat`: frag 個の連続した空きを block の境で取る。
  fragment の tail は作らない）。block は volume の中で 8 KiB の境に並ぶ。
- buffer cache の line は **leaf（whole disk）の LBA** の 4 KiB 単位（`disk_resolve_range`、`write_lines`）。
- よって volume の始まりが 8 sector の倍数（4 KiB に揃う。zedBSD の image は全て 2048 から）なら、1 つの line は 1 つの block の中にあり、
  普通の content の line を metadata の pin が止めることは無い（hold の場合は p051 の owed で扱う）。
- 揃わない volume（例: 古い MBR の sector 63 からの partition）では、block の境の line が 2 つの block をまたぐ。片方が directory・indirect・
  inode の block（journal の対象）なら、その変更の pin が隣の file の content の sector も止める。**変更が続く間は、次の transaction が前の
  unpin の前に同じ line を pin し直す（p051 の tag）ので、line はいつまでも書かれない**。ticket の「~1 秒の窓」より重い。
- 再現（下の表）: 直す前の kernel では fsync の戻りの 200 回のうち 156 回で、その時点で disk に無い content の sector があり、そこで電源を
  切ると file が壊れる（世代の混ざった sector）。

## 設計（ticket の候補 (3)。Q1 の同意 2026-10-04「案 (3) への切り替えに同意（技術の選択）」）

候補 (1)（fsync が file の dirty な line を知り、pinned なら次の commit を待つ）を先に作った（cbff2bc・37c79aa: commit と flush を最大 2 回
繰り返す）が、再現で不十分と分かった: 変更が続くと毎回の commit の間に次の transaction が line を pin し直すので、何回 commit しても line は
書かれない（最初の再現の cut で、2 世代前に fsync した file の tail がまだ世代 1）。**最終の code からは候補 (1) を外した**（2b24e1c。
履歴に残す。`buf_pinned_unjournaled`・`ufs_sync_waiting`・`UFS_SYNC_AGAIN_ROUNDS`・`IO_UFS_SYNC_AGAIN` は無い）。

候補 (3)（sector の単位で、journal の外の書き込みだけを cache の下から書く）:

- **buffer cache の記録**（`include/kern/buf.h` の `struct buf` に `b_unjournaled`、算術は新しい `include/kern/buf-unjournaled.h`）: line の
  block ごとの bit（4 KiB の line に 512 byte の sector なら 8 bit、最大 32）。普通の書き込み（`buf_write_context`）がその block の bit を立て、
  journal の pinned な書き込み（`write_lines`、`buf_write_pinned` から）がその block の bit を消す。line が clean に書かれると（`writeback_line`・
  `finish_run_line`）全部消える。変えるのは line の busy の持ち主だけで、`b_lock` の下（`mark_dirty` が dirty の印と同じ段で更新）。
- **新しい `buf_write_unjournaled(disk)`**（`src/kern/buf.c`）: disk の範囲の dirty な line のうち bit のある line を block の順に 1 回ずつ取り、
  busy を取って、bit の run だけを cache の下から書く（`backing_mutation_begin_disk_filesystem` ＋ `disk_write_direct_context`、遅延の
  write-back と同じ経路）。書いた bit を消す。他の持ち主の claim（swap・loop の image）に触れる run と disk の範囲の外の run は書かない
  （`writeback_line` と同じ扱い）。device の flush はしない。`IO_BUF_UNJOURNALED_WRITE`（`include/uapi/io-stats.h` の末尾に追加）で回数と
  byte を数える。
- **`ufs_sync`**（`src/drivers/fs/ufs.c`）: commit（p051 の owed の追加の commit を含む）→ `buf_sync`（pin の無い dirty な line を whole で
  書く。書けた line の記録は消える）→ `buf_write_unjournaled`（残る line、つまり pin された line の content の sector だけ）→ `disk_sync`
  （flush）。追加の commit は無い。

### Q1 の注意の 3 点

- (a) **journal の対象の sector は書かない**: run は bit の立つ block だけで、bit は「最後の書き込みが journal の外」の block にだけ立つ
  （pinned な書き込みが消す）。busy を持つ間は他の書き手がその line の data も記録も変えない（`acquire_line` の busy、`write_lines` も同じ）
  ので、書く data は記録と一致する。host 試験 `tests/host/unjournaled-test.c` で確かめた: pinned な書き込みが bit を消すこと、run が「最後に
  普通に書かれた block」と一致し、「最後に pinned で書かれた block」を決して含まないこと（6 回の書き込みの列 4096 通り）、sector 63 の
  partition の line（file の block 7 と directory の block 0–6）で run が file の block だけ。
- (b) **direct write と後の whole の write-back の順**: どちらも line の busy を持って行う（`buf_sync`・flusher・`buf_writeback_range` は
  `busy_acquire` の後に `writeback_line`、こちらも busy の中で書く）ので並ばない。direct に書く data は busy の中で取った cache の今の
  内容で、後の whole の write-back はそれ以降の cache（同じか新しい）を書く。古い content が新しいものを上書きすることは無い。
  `j3_home_direct`（p051）は busy を取らないが、書くのは閉じた transaction の logged range（pinned に書かれた sector、bit が無い）だけで、
  ここで書く sector とは重ならない。
- (c) **揃った volume で I/O は増えない**: `buf_sync` の後に残る dirty な line は pin された metadata だけで記録は空、
  `buf_write_unjournaled` は dirty の list を 1 回歩くだけで何も書かない。確かめ: sector 2048 の volume で同じ負荷のとき
  `IO_BUF_UNJOURNALED_WRITE` は calls=0 bytes=0（sector 63 では 601〜647 回、1.26〜1.34 MB）。fsync の速さは直す前と同じ（下の表）。

## fsync の保証（この直しの後）

`fsync` が 0 を返した後の電源断で: その file の content の sector（返る前に書かれた分）は、line が journal に pin されていても device にあり、
flush 済み。metadata は従来どおり journal の commit で durable。hold（解放した block への content）は p051 の owed の commit で扱う
（追加の commit を 2 回までしても line が pin され続けると hold は cache に残る。p051 の記録の例外のまま）。

## 確かめ

### build・host（P2、2026-10-04）

- kernel（`make -s ZEDBSD_CONFIG=config/ci/config-<p>.mk BUILD=build/p2-<p> build/p2-<p>/vmunix`）: amd64・pcat・rpi4 とも exit 0・warning 0・error 0。
- style: `plan/tools/style-check.py` と P2 の補助の checker で、変えた ufs.c・buf.c・buf.h・io-stats.h の新しい指摘 0（25b5533 と比べて）、
  新しい buf-unjournaled.h・unjournaled-test.c・syncprobe.c も 0。
- host: `cc -std=c11 -Wall -Wextra -Werror -I include -o unjournaled-test plan/ws073/tests/host/unjournaled-test.c && ./unjournaled-test`:
  `PASS`。syncprobe の host 版で setup・write・check、sector を 1 つ 0 にすると `torn` を返すこと。

### QEMU（P2 の QEMU 1 つ、KVM、NVMe。T には依頼していない（user の指示）。実機は未実施）

道具（新）: [tests/p053-fsync.sh](../tests/p053-fsync.sh)（MBR の partition を START＝63（揃わない）か 2048 から、16 の 1 block の file と
directory を交互に、syncprobe が file を順に in-place で書いて fsync、別の loop が directory に名前を作って消す。gdbstub で /jour の
`ufs_sync` の戻り（fsync の戻り）を最大 200 回見て、毎回 volume の dirty で pin された「2 つの block をまたぐ line」を guest の memory から
読み、syncprobe の stamp のある sector を host の disk image の同じ sector と比べる。違えば「fsync が返ったのに disk に無い content」。
CUT=unwritten は最初の 1 回で、CUT=last は 200 回目で machine を kill。再起動して journal を replay、`syncprobe check` で各 file が 1 つの
世代か、host で check-volume）、[tests/syncprobe.c](../tests/syncprobe.c)・[tests/syncprobe-build.sh](../tests/syncprobe-build.sh)。
直す前の kernel は 25b5533（main、p051 まで）の 4 file で DWARF の image を作った（`build/p2-b163-base`）。

| kernel | START | CUT | fsync の戻りで disk に無い content | crash 後の file | volume | `IO_BUF_UNJOURNALED_WRITE` |
| --- | --- | --- | --- | --- | --- | --- |
| 直す前 | 63 | unwritten | 5 回目で 1 sector | **torn 1**（f8: sector 0 が世代 41、他は 42） | orphan inode 1（下） | —（無い） |
| 直す前 | 63 | last | **200 回中 156 回** | **torn 6** | UFS OK | — |
| 直した後 | 63 | unwritten | 200 回中 0 回 | torn 0 | UFS OK | 647 回、1,338,880 byte |
| 直した後 | 63 | any（最初の戻り） | — | torn 0 | UFS OK | 601 回、1,260,032 byte |
| 直した後 | 2048 | any | — | torn 0 | UFS OK | **0 回、0 byte** |

- 直す前の cut の判定外の 3 回（CUT=any・waiting の旧版）: 最初の戻りで切ると torn 0 が 3 回（その瞬間に disk に無い content が無かった）。
  そのため上の CUT=unwritten（disk と比べる）に改めた。候補 (1) の kernel（37c79aa）の cut では torn 8（世代 1 の tail が残る）。
- 回帰（直した後の image `build/p2-b135`）: `crash-test.sh … 9`: HOLDS 3182 = PREFIX 3182、`UFS OK`。`p051-window.sh`: PASS（names 2706、
  volume・root `UFS OK`）。fsprobe の churn（`CHURN=1 LOAD_JOBS=2 RUN_SECONDS=45`）を直す前と後で続けて: stat 最長 103 ms / 102 ms、
  1 秒超 0 / 0、fsync の書き手 80 / 81 回（同じ）。

### 別の観察（この bug の外、Q1 へ）

直す前の kernel の CUT=unwritten の回で、host の check-volume が `orphan allocated inodes: [300]`（regular file、nlink 0、size 0 で
bitmap は使用中）。churn の `rm` で名前を失った file の inode の解放が、cut の時に disk に無かった（名前の削除と inode の解放が別の
transaction）と見る。直した後の 4 回と直す前の他の 3 回は `UFS OK`。p053 の変更（content の書き方）とは関係しない。再現の条件と
扱い（fsck の orphan の処理か、解放を同じ transaction に入れるか）は未調査。disk: `build/p2-b135/p053-before63-u1/disk.img`（worktree の build）。

## 未実施・残り

- 実機。T1 の試験（user の指示で除外）。pc98・intelmac の build（buf.c は共通、amd64・pcat・rpi4 で確かめた）。
- syncprobe の check は file ごとに 1 block（8 KiB）。大きい file・indirect の block の隣は試していない（仕組みは同じ）。
- 使い捨て: `build/p2-b135`（直した後の DWARF の image、p053 の出力）、`build/p2-b163-base`（直す前）、`build/p2-b135-run`。

## 判定（Q1、2026-10-04）

cleared。P2 の自分の QEMU（T には依頼しない形）: MBR の partition を 63 から始めた volume で、直す前（25b5533）は 200 回の fsync の戻りのうち 156 回で content が disk に無く、crash の後 16 個中 6 個の file が欠けた。2b24e1c の後は 0/200・欠け 0・UFS OK、direct の書き 647 回。2048 から始めた volume では direct の書き 0（余計な I/O なし）。crash-test.sh・p051-window.sh PASS、fsprobe の churn は前後で同じ。kernel は amd64・pcat・rpi4 で warning 0（main でも確認）。実機は未実施。別の観察（直す前の kernel の 1 回の orphan の inode）は BUG-164。
