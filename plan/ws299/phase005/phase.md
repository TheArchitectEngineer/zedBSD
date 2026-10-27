<!-- awesome-plan project=zedbsd record=ws299-p005 -->

# ws299-p005 UFS の file の連続の読み書き（先読みの窓・大きな request・書きの合流）

- 親: [WS299](../ws.md)。Queue: q1634 / q1634-i01（担当 P1）。前提: p002・p004（raw の先読み）。
- Status: planning（2026-10-28 P1: 調べと計画の案だけ。設計・design-reviewer・実装は未着手）
- Disposition: normal

## 調べた事実（2026-10-28、読みだけの調べ、tree は agent/p1-ws299 36d0d7b5d）

- 読み（`dd if=/mnt/file bs=1m`）: `sys_read_call` が 1 MiB の bounce（`src/kern/syscall.c:93`・2851-2862）→ `file_io_transfer` → VM object の cache（`src/kern/file.c:1199-1209`・3037-3075、`vm_object_read_coherent_useful` `src/kern/vm/object-content.c:587`）。miss は 1 回 64 KiB（`object-cache.c:816` `KERN_IO_BATCH_MAX`、`VM_OBJECT_CACHE_PAGES` 16）で、`io_pool` の scratch へ読み（:856・962）、scratch → page（:987）、scratch → syscall の buffer（:1009）。
- UFS: `pread_inode`（`src/drivers/fs/ufs.c:6107`）、連続の run は `KERN_IO_BATCH_MAX / bsize` まで（:6047-6060）、block ごとに `bmap`（:6087、間接は 512 byte の metadata 読み :5292）。disk へは `disk_read` → `buf_read` → 64 KiB の page 列（buffer cache の line と VM の page の二重の cache）。1 MiB の read で 64 KiB の同期の request が 16 回、QD1、写しが 4 回。
- 既存の file の先読み（`src/kern/readahead.c`）は UFS にも効くが、1 回 64 KiB（`READAHEAD_REQUEST_MAX`）、demand の I/O の間は始めない（:773）、disk ごとに worker 1 本・slot 2（:319）、scratch に読んで写す（:819・832）。1 MiB の読みにはほぼ効かない。
- 書き（`dd of=/mnt/file bs=1m`）: 伸びる書きは毎回 UFS へ（`file.c:1810-1826`、`ufs.c:8655`）、新しい block の割当は 64 KiB ごと（`UFS_ALLOCATION_BYTES`、`ufs.c:165-166`・7315-7612、cylinder group・leaf・inode を毎回）。j3 の既定では content は `buf_write_context` で line を dirty にし（dirty 32 MiB まで、`buf.c` `BUF_DIRTY_LIMIT`）、それを越えると 64 KiB の同期の書き。flusher は 1 line（4 KiB）ずつ同期で書く（合流なし）。
- p004 の raw の先読み（`buf_readahead_observe`）は devfs の raw の open だけに効き、UFS の読みには効かない（mount された区画には先読みしない、`disk_readahead_private`）。
- 5330 の今の数（`evidence/zb-p004f-summary.md`）: UFS の書き 353・378 MB/s（Debian の ext4 の書き 1.6 GB/s、`evidence/deb1-summary.md`）。UFS の file の読みは zedBSD・Debian とも未測定。

## 計画の案（設計の前の案。design-reviewer と Q1 の判断の前）

1. 測る: `bench-zedbsd.sh` に UFS の file の冷えた読み（write の段で書いた file を、次の起動の read の段の最初に読む、2 GiB）を、`bench-debian.sh` に ext4 の冷えた読み（`echo 3 > /proc/sys/vm/drop_caches` の後）を足す。Debian の 95% の基準をここで決める。
2. 読み: (a) VM object の miss の run を 64 KiB から 256 KiB〜1 MiB に（`KERN_IO_BATCH_MAX` を使う所を分ける、scratch を経ずに page 列で読む口を検討）、(b) file の先読みの窓を 1〜2 MiB にし、demand の I/O と重ねられるように（`readahead_demand_begin` の扱い）、worker を disk ごとに 1 本の制限から外す。raw の先読み（p004）と同じく、line の確保の cache_lock の往復を減らす（`recycle_line_locked`・`copy_cached_lines` を使う）。
3. 書き: (a) flusher と `buf_sync` が隣り合う dirty の line を page 列の run（256 KiB まで）にまとめて書く、(b) UFS の割当を 64 KiB より大きく（cylinder group・inode の更新を run ごとに 1 回）、(c) `BUF_DIRTY_LIMIT` を cache の大きさに合わせる。
4. 試験: host（合流の計画の純粋な関数）、QEMU（boot・nvme-shapes・grow-reboot・UFS の cksum と fsync の後の reboot）、5330（`nvme-bench-5330.sh … read write`）。

## 判断が要る点

- 2(a) は VM と UFS の共通の口（`KERN_IO_BATCH_MAX` の意味）を変える。範囲が WS299 の「UFS の cluster」に入るか、VM の WS を別に立てるかを Q1 に聞く。
- HAL の API は変えない見込み。

## 再開点（2026-10-28、P1）

- p004 の後に P1 の context が少なくなったので、p005 はこの調べと案で止めた。次: 1（測る）→ 案の design-reviewer → 実装。
