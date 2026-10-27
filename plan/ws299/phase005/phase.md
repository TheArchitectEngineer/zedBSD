<!-- awesome-plan project=zedbsd record=ws299-p005 -->

# ws299-p005 UFS の file の連続の読み書き（先読みの窓・大きな request・書きの合流）

- 親: [WS299](../ws.md)。Queue: q1634 / q1634-i01（調べ）→ q1640 / q1640-i01（設計・実装、担当 P1）。前提: p002・p004（raw の先読み）。
- Status: in-progress（2026-10-28 P1: 設計（下の D1〜D7）、design-reviewer、実装）
- Disposition: normal

## 調べた事実（2026-10-28、読みだけの調べ、tree は agent/p1-ws299 36d0d7b5d）

- 読み（`dd if=/mnt/file bs=1m`）: `sys_read_call` が 1 MiB の bounce（`src/kern/syscall.c:93`・2851-2862）→ `file_io_transfer` → VM object の cache（`src/kern/file.c:1199-1209`・3037-3075、`vm_object_read_coherent_useful` `src/kern/vm/object-content.c:587`）。miss は 1 回 64 KiB（`object-cache.c:816` `KERN_IO_BATCH_MAX`、`VM_OBJECT_CACHE_PAGES` 16）で、`io_pool` の scratch へ読み（:856・962）、scratch → page（:987）、scratch → syscall の buffer（:1009）。
- UFS: `pread_inode`（`src/drivers/fs/ufs.c:6107`）、連続の run は `KERN_IO_BATCH_MAX / bsize` まで（:6047-6060）、block ごとに `bmap`（:6087、間接は 512 byte の metadata 読み :5292）。disk へは `disk_read` → `buf_read` → 64 KiB の page 列（buffer cache の line と VM の page の二重の cache）。1 MiB の read で 64 KiB の同期の request が 16 回、QD1、写しが 4 回。
- 既存の file の先読み（`src/kern/readahead.c`）は UFS にも効くが、1 回 64 KiB（`READAHEAD_REQUEST_MAX`）、demand の I/O の間は始めない（:773）、disk ごとに worker 1 本・slot 2（:319）、scratch に読んで写す（:819・832）。1 MiB の読みにはほぼ効かない。
- 書き（`dd of=/mnt/file bs=1m`）: 伸びる書きは毎回 UFS へ（`file.c:1810-1826`、`ufs.c:8655`）、新しい block の割当は 64 KiB ごと（`UFS_ALLOCATION_BYTES`、`ufs.c:165-166`・7315-7612、cylinder group・leaf・inode を毎回）。j3 の既定では content は `buf_write_context` で line を dirty にし（dirty 32 MiB まで、`buf.c` `BUF_DIRTY_LIMIT`）、それを越えると 64 KiB の同期の書き。flusher は 1 line（4 KiB）ずつ同期で書く（合流なし）。
- p004 の raw の先読み（`buf_readahead_observe`）は devfs の raw の open だけに効き、UFS の読みには効かない（mount された区画には先読みしない、`disk_readahead_private`）。
- 5330 の今の数（`evidence/zb-p004f-summary.md`）: UFS の書き 353・378 MB/s（Debian の ext4 の書き 1.6 GB/s、`evidence/deb1-summary.md`）。UFS の file の読みは zedBSD・Debian とも未測定。

- 2026-10-28 追記（P1、`zb-p004h` の io-stats を事象ごとに分けた）: UFS の 256 MiB の書きで `IO_BUF_WRITE` は 4440 回（1 回 約 60 KiB、遅らせる書き）、`IO_DRIVER_WRITE` は 57350 回 × 4 KiB。dirty が上限 32 MiB に達した後、`buf_write_context` は line ごとに「遅らせる → 印を付けた後に上限を越えた → その line だけを同期で書く」になり、4 KiB の同期の書きが 224 MiB / 4 KiB ≒ 57344 回並ぶ。flusher も line ごと（`flush_aged` → `buf_writeback` → `writeback_line`、4 KiB）に書く。`IO_BUF_SINGLE_GEOMETRY` 65357 回は書きではなく、4 KiB の書きごとの UFS の間接 block の窓の読み（`IO_UFS_INDIRECT_WINDOW` 64610 回 × 512 byte）。
- 2026-10-28 追記: 測り直し（fstrim、p6 の discard、休み、`plan/ws299/tests/bench-*.sh` の改訂、下の「測定の条件」）で、Debian の ext4 の 1 GiB の冷えた読み 1.5 GB/s、ext4 の 1 GiB の書き（fsync 込み）1.5 GB/s、raw の 1 MiB の direct の書き 1.6〜1.9 GB/s（256 MiB ずつ）、page cache 経由の 1 GiB（fsync 込み）1.5 GB/s。

## 測定の条件（2026-10-28 改訂、ユーザー「P1はfstrimを使った上で、SSDの消去の影響をなるべく排除して、再度目標達成まで継続してください。」）

- 以前の読みの測定の位置（disk の 100 GiB の辺り）は Debian の ext4 の空き（block の 97% が未使用）で、週ごとの `fstrim.timer`（2026-10-27 00:31 に走った）で trim 済みだった。trim 済みの block は drive が flash を読まずに返すので、読みの数は実際の flash の読みではなかった（Debian・zedBSD とも）。
- 読みの位置を、Debian の ext4 の上の 12 GiB の乱数の file `/home/awe/ws299/data.bin`（2026-10-28 作成、消さない）の extent が並ぶ範囲にした: disk の 25681 MiB から 8064 MiB（p2 の block 6324224〜8388607。間の 128 MiB の隙間 3 つと 8 MiB の 2 つは ext4 の inode の表などの使用中の block）。`R0_MIB`・`R_MIB`。
- 各 mode の前に Debian で `fstrim -av` と 60 秒の休み。書きの各段の前に p6（`zedBSD-nvme-swap`、PARTLABEL・番号・Debian の swap・mount でないことを確かめる）を `blkdiscard` し、swap の頭 4 KiB だけを書き戻す（`bench-debian.sh p6fresh`）、30 秒の休み。p1〜p5 は discard しない。zedBSD の UFS は、同じ file を上書き（`ufsover`、同じ LBA）と、消して新しく書く（`ufsnew`、UFS は TRIM を送らないので、以前に書かれた空きの LBA に書く）の両方を測る。
- 乱読みは `plan/ws299/tests/randread.c`（Linux と zedBSD の両方で build、thread の数 = 深さ、各 thread は pread を 1 つずつ）。Debian は direct（`O_DIRECT`、fio と同じ数になる事を確かめた）を基準にする。Debian の page cache 経由（`cached`）は 8 GiB の範囲の大半が RAM に載るので比べない（参考）。zedBSD は buffer cache（1 GiB）を通るので、その hit の数を snap で記録する。

## 設計（2026-10-28、P1）

### D1. dirty の上限を cache の大きさに合わせる（`src/kern/buf.c`）

- `BUF_DIRTY_LIMIT`（32 MiB 固定）を、cache の上限の 1/4 にする（`dirty_limit`、`buf_init` と `buf_set_max_bytes` で計算、`cache_lock` の下で書き、読みは atomic）。5330（cache 約 985 MiB）で約 246 MiB。cache が 32 MiB の機械（RAM 256 MiB）で 8 MiB になり、dirty が cache を埋め尽くして新しい line が取れなくなる事が無くなる（p004 の 32 MiB の cache の書きが止まった件）。
- 背景の書き戻しの閾値 `dirty_background` = `dirty_limit / 2`。

### D2. 隣り合う dirty の line をまとめて書く（cluster）

- `buf_writeback(buffer)`（遅らせた書きを書き手の代わりに書く口。flusher・`buf_sync`・`buf_writeback_range`・`buf_writeback_cached`・reclaim が使う）を `writeback_cluster` にする。呼び手は buffer の busy と参照を持つ。
- 条件（全部を満たす時だけまとめる。満たさなければ今の `writeback_line(buffer, NULL, 1)`）: leaf が page 列を受ける（`disk_page_vector_supported`）、block の大きさが page 以下、line が page の大きさ（`b_memory.size == KERN_PAGE_SIZE`、`b_block_count` が 1 page）、dirty・valid・journal の pin が無い。
- 前と後ろへ、`hash_find_locked` で同じ leaf の隣の line を `cache_lock` の 1 回の保持で参照（`refcount_get`・`lru_remove_locked`）し、各 line の `b_lock` の下で「busy でない・inflight でない・dirty・valid・pin なし・page の大きさ」なら busy を取る（待たない。だめな line で止まる）。最大 `BUF_CLUSTER_LINES` = 256（1 MiB、NVMe の BIO の上限と同じ）。
- 範囲全体に他の claim（swap file・loop の image）の block があれば（`backing_claim_find_extent_owner`）まとめず、buffer だけを今の道で書く（BUG-073 の扱いを保つ）。
- 書き: 各 line の `b_dirty_generation` を記録して `BUF_IO_WRITING`・inflight、`backing_mutation_begin_disk_filesystem`（範囲全体、今の `writeback_line_whole` と同じ guard）→ `disk_transfer_pages_context(BIO_WRITE)` → guard の終わり。完了した prefix の line は `finish_run_line`（世代が同じなら clean、`b_unjournaled = 0`）、残りは誤り（dirty のまま、今の失敗と同じ）。guard の失敗・driver の `EOPNOTSUPP`（何も送っていない）は、line を idle に戻して buffer だけを今の道で書く。
- 後ろへ広げるので、古い line が範囲の中ほどでも run の頭から書ける。他の line の busy は取れなければ待たないので、「line を持ったまま他を待たない」規則を保つ。
- `IO_BUF_RUN_WRITE` に数える（cluster の書き。今の write-through の run と同じ事象）。

### D3. 背景の書き戻しと書き手の手伝い

- dirty が `dirty_background` を越えたら、書き戻しの worker を起こす。worker は p004 の先読みの worker（`BUF_READAHEAD_WORKERS` = 4）と同じ thread を使い、先読みの仕事が無い時に「一番古い、pin の無い、busy でない dirty の line」（`dirty_head` から try-busy、待たない）を cluster で書く。dirty が `dirty_background / 2` 未満になるか、書ける line が無くなるまで続ける。書いた leaf は記録し、仕事が尽きた時に `bio_flush`（今の flusher の pass と同じ耐久の扱い）。
- `buf_write_context` の遅らせる書き（`DISK_WRITE_CACHED` の disk）で dirty が `dirty_limit` を越えたら、書き手は line を持つ前に、古い dirty の cluster を自分で（try-busy、待たない）書いて `dirty_limit` 未満に戻す（手伝い）。書ける line が無ければ今の形（その line を同期で書く）に落ちる。眠って待つ事はしない（UFS の lock を持った書き手が、worker の進みを待って止まらない）。
- 今の「上限の後は line ごとに同期で書く」形は、手伝いが何も書けない時の最後の道として残る。

### D4. raw の write-through の run を大きく、NVMe で並べる

- page 列の run の最大（`BUF_RUN_PAGE_LINES`）を 64（256 KiB）から 256（1 MiB）に。raw の 1 MiB の write は 1 つの BIO になる。先読みの仕事の大きさは `BUF_READAHEAD_RUN_BYTES`（256 KiB）のまま分ける。
- NVMe: page 列の 1 command を `NVME_IO_PAGE_COMMAND_BYTES` = 256 KiB までにし、1 MiB の BIO を同時に 4 command（`NVME_IO_PIPELINE_DEPTH` = 4）で流す（Linux の 5330 は `max_sectors_kb` 128）。copy と device の書きの重なりはこの段では作らない（測って足りなければ D4b: write-through の run を worker に渡して次の run の copy と重ねる）。

### D5. UFS の file の先読み（buffer cache へ、`src/drivers/fs/ufs.c`・`src/kern/buf.c`）

- buf.c に `buf_prefetch(disk, block, count)`: 呼び手（file system）が自分の data の block と知っている範囲を、先読みの worker の仕事にする（流れの検出・`disk_readahead_private` は通さない。file system の demand の読みと同じ line を作るだけなので、direct の書きで古くなる危険は demand の読みと同じ）。止め（D3 of p004）・admission・memory の扱いは p004 の仕事と同じ。queue が満ちていれば頼まない。
- UFS: inode ごとの流れ（`next`・`issued`・`sequential`、inode の私有の構造、更新は spinlock）。`pread_inode` の読みが前の読みの終わりから窓（2 MiB）の内なら連続と数え、2 回続いたら `[max(issued, 読みの終わり), 読みの終わり + 窓)` を `bmap` で disk の run（最大 256 KiB）に分けて `buf_prefetch`。VM の先読み（`readahead.c`、64 KiB、demand より少し先）の読みも同じ inode の読みとして来るので、「窓の内」で連続とみなし、`issued` は単調に進める。
- 穴・fragment の末尾・間接 block の境は run を切る。bmap の誤りは先読みをやめるだけ。

### D6. VM の miss の run を大きく（`src/kern/vm/object-cache.c`、`KERN_IO_BATCH_MAX` の使い方）

- VM の object の miss の run（今 64 KiB = `KERN_IO_BATCH_MAX`、16 page）と UFS の連続の run（`content_run_bytes`、64 KiB）を、それぞれの定数（`VM_OBJECT_CACHE_RUN_MAX`・`UFS_CONTENT_RUN_MAX`）に分けて 256 KiB にする。`io_pool` の scratch が 256 KiB を貸せない時は今の 64 KiB に落ちる。D5 の後に測って、効く時だけ入れる（2026-10-28 Q1 の範囲の決定: VM と UFS が共有する 64 KiB の上限を変える事は WS299 の中で P1 が決める）。

### D7. 書きの metadata の読み

- 4 KiB の書きごとの間接 block の窓の読み（`IO_UFS_INDIRECT_WINDOW`）を、D1〜D3 の後の測定で sys の時間が目立つ時だけ減らす。

## 変えない物

- HAL の API、UAPI（io-stats の事象は増やさない）、UFS の形式・journal の順序（pin の line は書かない）、NVMe の recovery。

## 受け入れ

1. 5330: UFS の file の 1 GiB の書き（`ufsover`、sync 込み）と冷えた読み（`ufsread`）が、同じ条件の Debian の ext4（`ext4-w1m`・`ext4-r1m`）の 95% 以上。raw の書き（`raw0..3` と sync の和、`rawtotal`）が Debian の `b-w1m` の 95% 以上、各 256 MiB が `d-w*` の中央値の 95% 以上。`ufsnew` の数も記録する。
2. 32 MiB の cache の QEMU で UFS の 200 MiB の書きが止まらない（D1）。
3. QEMU: boot-test、`qemu-readahead.sh`・`qemu-nvme-shapes.sh`・`qemu-grow-reboot.sh`・`qemu-cache-stress.sh`、加えて UFS の書きと sync・reboot の後の cksum。
4. build の warning 0（config-5330・pcat・visionfive2・rpi4・m68k-virt）。host 試験（cluster の範囲の計算、UFS の流れの検出）。

## 計画の案（2026-10-28 q1634 の時点の案、上の設計で置き換えた）（設計の前の案。design-reviewer と Q1 の判断の前）

1. 測る: `bench-zedbsd.sh` に UFS の file の冷えた読み（write の段で書いた file を、次の起動の read の段の最初に読む、2 GiB）を、`bench-debian.sh` に ext4 の冷えた読み（`echo 3 > /proc/sys/vm/drop_caches` の後）を足す。Debian の 95% の基準をここで決める。
2. 読み: (a) VM object の miss の run を 64 KiB から 256 KiB〜1 MiB に（`KERN_IO_BATCH_MAX` を使う所を分ける、scratch を経ずに page 列で読む口を検討）、(b) file の先読みの窓を 1〜2 MiB にし、demand の I/O と重ねられるように（`readahead_demand_begin` の扱い）、worker を disk ごとに 1 本の制限から外す。raw の先読み（p004）と同じく、line の確保の cache_lock の往復を減らす（`recycle_line_locked`・`copy_cached_lines` を使う）。
3. 書き: (a) flusher と `buf_sync` が隣り合う dirty の line を page 列の run（256 KiB まで）にまとめて書く、(b) UFS の割当を 64 KiB より大きく（cylinder group・inode の更新を run ごとに 1 回）、(c) `BUF_DIRTY_LIMIT` を cache の大きさに合わせる。
4. 試験: host（合流の計画の純粋な関数）、QEMU（boot・nvme-shapes・grow-reboot・UFS の cksum と fsync の後の reboot）、5330（`nvme-bench-5330.sh … read write`）。

## 判断が要る点

- 2(a) は VM と UFS の共通の口（`KERN_IO_BATCH_MAX` の意味）を変える。範囲が WS299 の「UFS の cluster」に入るか、VM の WS を別に立てるかを Q1 に聞く。
- HAL の API は変えない見込み。

## 再開点（2026-10-28、P1）

- p004 の後に P1 の context が少なくなったので、p005 はこの調べと案で止めた。次: 1（測る）→ 案の design-reviewer → 実装。
