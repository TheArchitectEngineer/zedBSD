<!-- awesome-plan project=zedbsd record=ws307-p006 -->

# ws307-p006 kernel の 4096 byte の sector（FAT・UFS・swap）

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §5.9（U17「WS307 で 4096 byte に対応」）。Queue: q1665 / q1665-i03（担当 P4、2026-10-29、worktree p4-ws307g）。
- Status: in-progress（設計と段 1・2 に着手）
- 承認: 2026-10-29 ユーザー「P4にWS307」（Q1 の中継）。HAL の API には触れない。
- 一緒に行う物: p005 の残り（mkfs の native UFS の fragment 4096 の profile）、p008 の 4096 の段（loader の FAT の探索）。

## 調べた事（2026-10-29、基点 92e91e61b）

- disk の口（`src/kern/disk.c`）: `disk_read`・`disk_read_direct`・`disk_write_filesystem_context`・`disk_write_direct_context` などの `block`・`count` は device の block（`d_block_size`）の単位。512 byte の単位から大きい block へ換える層も、block の一部の read-modify-write も無い。partition は親の block の大きさを継ぐ。
- FAT（`src/drivers/fs/fat.c`）: 中の sector の番号は全て 512 byte の単位（BPB の byte／sector を `sector_scale` 1・2 に換える、`parse_bpb`）。disk の口へそのまま渡す（`fat_sector_read`・`fat_sector_write`・`fat_sectors_write`、0 埋めの `disk_resolve_range`、loop の転送、`fat_identify`）。512 でない disk は probe（:911）・loop の map（:963）・identify（:11842）で拒む。512 byte の固定の buffer（`fat_identify` の boot、`parse_bpb` の bpb、sector cache）。
- UFS（`src/drivers/fs/ufs.c`）: LBA は `fragment << fsbtodb` の 512 byte の単位（`512 << fsbtodb == fsize`）。512 でない disk は identify（:2853）・extent（:5518）・superblock の読み（:16961）で拒む。fragment より小さい読み書き: superblock の locator（1 sector）、bmap の memo（1 sector）、edge の読み（4 sector まで）、writeback の間接 block（1 sector ずつ）、j3 の header・descriptor・commit（512 byte の record）。snapshot の擬似 disk は 512。
- swap: partition の源は既に一般（`SWAP_PAGE_SIZE % d_block_size`、`blocks_per_page`）。file の源（`swap-source.c:246`）は extent を 512 の単位で受け、そのまま device の block として使う。`file.c` の format の予約（`file_format_finalize`、:2931）も同じ。
- 識別: `block-identity.c` は一般（block の大きさの bounce）。FAT・UFS の identify が 512 でないと EOPNOTSUPP。

## 設計（技術の判断 T28）

- **512 byte の sector の口**（`src/kern/disk-sector.c`、`include/kern/disk-sector.h`、新規）: FAT・UFS・swap の file の源が今の 512 byte の単位のまま disk を読み書きする口。`d_block_size` が 512 の disk では今の関数をそのまま呼ぶ（今の disk の振る舞いは変わらない）。512 より大きい block（2 の冪、page 以下）の disk では、block に揃った部分を block の単位に換えて渡し、端の一部の block は bounce の buffer に block を読み、変えて書く（read-modify-write）。cached と direct の 2 つの道を今の関数と同じに分ける。
- read-modify-write の排他: 呼ぶ側の lock（FAT の mount の lock、UFS の journal・superblock の lock）に任せる。同じ block の他の 512 byte の部分を別の書き手が同時に書く使い方を作らない: UFS は fragment が `d_block_size` 以上の volume だけを mount する（spec §5.9 の fragment 4096 の profile、T19）ので、一部の block の書き込みは journal と superblock の 512 byte の record だけ。FAT は BPB の byte／sector が `d_block_size` 以上の volume だけ（4Kn の disk の 512 byte の sector の FAT は作られない形なので拒む）。
- FAT: BPB の byte／sector に 2048・4096 を足す（`sector_scale` 4・8）。512 でない disk の拒みを「BPB の sector が `d_block_size` より小さい」に換える。disk の口を sector の口に換える。loop の map は 512 の disk だけのまま（範囲外）。
- UFS: identify・superblock・extent の拒みを「`fsize` が `d_block_size` より小さい」に換える。disk の口を sector の口に換える。
- swap の file と format の予約: extent（512 byte の単位）を device の block に換える。block に揃わない extent は EINVAL。
- 受け入れ: QEMU の `-device nvme,logical_block_size=4096,physical_block_size=4096` の disk に、guest の `diskpart`・`mkfs -t fat32`・`mkfs -t ufs --profile=native`（fragment 4096 の profile、p005 の残り）・`mkswap` で作り、mount・読み書き・`swapon`、host の `fsck.fat` と UFS の reader で読み戻す。512 の disk の回帰（boot-test の login prompt）。

## 段

1. sector の口（disk-sector.c）と、その host の試験（偽の disk で揃った・端の読み書き）。
2. FAT（BPB 4096、probe・identify・I/O）。
3. UFS（fsize ≥ block の検査、I/O の全ての口）。
4. swap の file の extent と format の予約。
5. mkfs の native UFS の fragment 4096 の profile（p005 の残り）。
6. QEMU の 4096 の disk の試験（1 本の script）と 512 の回帰。p008 の 4096 の段へ。
