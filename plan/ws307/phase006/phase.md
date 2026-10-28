<!-- awesome-plan project=zedbsd record=ws307-p006 -->

# ws307-p006 kernel の 4096 byte の sector（FAT・UFS・swap）

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §5.9（U17「WS307 で 4096 byte に対応」）。Queue: q1665 / q1665-i03（担当 P4、2026-10-29、worktree p4-ws307g）。
- Status: in-progress（段 1〜4 を実装、FAT と swap は QEMU の 4096 の NVMe で受け入れ。残り: 段 5（mkfs の UFS の fragment 4096）と UFS の 4096 の試験、USB の 4096、p008 の 4096 の段）
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

## 結果（2026-10-29、P4、q1665-i03）

- 段 1（commit b8d8ae676）: `src/kern/disk.c` に `disk_sector_transfer`・`disk_sector_supported`、`include/kern/disk.h` に `DISK_SECTOR_SIZE`。新しい file にせず disk.c に置いた（各 platform の kernel の source の一覧（sparcv9 を含む）に触れないため）。
- 段 2（commit 0bebb4b73）: FAT。probe・identify は `disk_sector_supported`、BPB の byte／sector に 2048・4096（`fat_sector_scale`）、BPB の sector が `d_block_size` より小さい volume は拒む、volume の大きさは disk の 512 byte の sector の数（`fat_disk_sectors`）と比べる、`fat_sector_read`・`fat_sector_write`・`fat_sectors_write`・identify の読みを sector の口に、0 埋めの線の揃えは 4096 の disk では run の始まり（partition は block に揃うので同じ）。loop の map は 512 の disk だけのまま。
- 段 3（commit 0bebb4b73）: UFS。identify・superblock・extent の拒みを `disk_sector_supported` に、`fsize < d_block_size` の volume は EOPNOTSUPP、disk の大きさの比較は `ufs_disk_sectors`、`observed_disk_read`・journal の読み書き・`write_sectors`・j3 の書き込みと home への写し（direct）・replay を sector の口に、group の view の読みは揃った run を block に換える（`ufs_disk_read_view`）、j3 の home への写しの guard は block に換えた範囲（`ufs_sector_blocks`）。
- 段 4（commit ba7daed55）: swap の file の源は FAT・UFS の extent（512 の単位）を device の block に換えて持つ（block に揃わなければ EINVAL）、page の I/O は block の単位。`file.c` の format の予約も同じ（claim は device の block）。
- NVMe（この試験に要った）: `include/drivers/pci/pci-nvme-protocol.h` の namespace の検査が LBA 512 だけを受けていた（reason 0x40）。512〜4096（shift 9〜12）を受ける。driver の他の所は `namespace_block_size` で一般。`plan/ws299/tests/qemu-nvme-shapes.sh` の comment「the driver takes only 512-byte blocks」は古くなる（ws299 の物なので Q1 に）。

## 確かめ（2026-10-29、P4）

- build: `make -j48 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws307-main build/ws307-main/vmunix` rc 0、`warning:`・`error:` 0（段ごとに 4 回）。SSH の guest の image（`sh plan/tools/guest/build-ssh-image.sh build/ws307-ssh`）rc 0、warning は make の jobserver の 1 行だけ。
- QEMU（KVM、OVMF、`sh plan/ws307/tests/p006-4k.sh build/ws307-ssh/hdd-image.img`、新規。2 台目に `-device nvme,logical_block_size=4096,physical_block_size=4096` の 1 GiB）: `p006-4k: PASS build/ws307-test/p006-4k.uYuD9a`。guest で `diskpart init`（ESP 512 MiB・swap 128 MiB、4096 の sector）、`mkfs -t fat32`、mount、3 MB の乱数の file と 300 file の directory、umount・再 mount で cksum が同じ、FAT の上に swap file（dd・`mkswap`・`swapon`・`swapoff`）、swap の partition（`mkswap /dev/nvme1n1p2`・`swapon`・`swapoff`）。host の mtools で同じ volume を読み、file の cksum が guest と同じ（`HOST CKSUM 3150429007 3182592`）。
- 512 の回帰: 同じ image の `plan/tools/boot-test.sh`（uefi-nvme）login prompt PASS（`build/ws307-test/p006-boot.ny8kCK/login.png`）。上の試験も 512 の NVMe の UFS の root から起動し SSH で動いた。
- 未実施: UFS の 4096（mkfs の native UFS の fragment 4096 の profile が要る、段 5）、USB（`usb-storage` の 4096）、host の `fsck.fat`（host に無い）、FAT32 の 4Kn の ESP からの起動（p008 の 4096 の段）。

## q1665-i04 の結果（2026-10-29、P4、worktree p4-ws307h）

- 段 5（commit bfa7afcdb、p005 の残り）: `mkfs -t ufs --profile=native` は disk の logical sector に合わせて fragment を選ぶ（512 → 1 KiB・block 8 KiB、今の形のまま。4096 → 4 KiB・block 32 KiB）。group の中は 64 KiB の boot の域・superblock の block・map の block・inode の表・data（4 KiB では fragment 16・24・32・48）。group は 1 KiB の profile の最大と同じ約 62 MiB（fpg 15984）で inode 256 個、inode の密度は今と同じ。8 KiB より大きい block は 8 KiB の buffer で書き、残りを 0 にする。persistence の tail（ZUJ2・ZSL1）の位置は `fragments × fsize / 512`。`--check-size --sector-size=4096` も native を受ける。`ufs_format_native_*` は sector の大きさを引数に取る（呼ぶのは block-command.c だけ）。
- host（`sh plan/ws307/tests/host/mkfs-ufs-native-test.sh`、新規）: `mkfs-ufs-native-test: PASS`。1 KiB の ordinary・feature・native（4 MiB〜64 GiB の 10 の大きさ）が d94ceac00 の formatter と byte で同じ。4096 の native（4 MiB〜100 GiB の 6 つ）と 512 の 2 つを formatter の verify、mkfs の superblock の decoder、capacity の報告（free の block・inode が superblock と同じ）、loader の reader（`bootloader/common/ufs-read.c`）の mount（4096 と 512 の media）で確かめた。target の `build/ws307h/bin/mkfs` は warning 0。
- QEMU（KVM、OVMF、`sh plan/ws307/tests/p006-4k.sh build/ws307h-ssh/hdd-image.img`、UFS の段を足した、commit cbe44934e）: `build/ws307-test/p006-4k.O52KLU`。FAT・swap の段は前と同じく ok。4096 の NVMe の 3 つ目の partition に `mkfs -t ufs --profile=native`・mount・9.6 MB の乱数の file（直接の block の先、間接 block）・300 file・深い directory・umount・再 mount・cksum が同じ（ok）。host で loader の reader（`plan/ws307/tests/host/ufs-cat.c`、4096 の block）が `/random.bin`・`/many/f299.txt` と、`sync` だけして mount のまま guest を止めた `/synced.bin` を guest と同じ cksum で読んだ（sync が home の位置まで書く確かめ、p008 の残りの一つ、4096 の disk で）。
- UFS の上の swap file が 4096 の disk で `mkswap: Invalid argument`（上の試験の唯一の fail）。原因: `src/kern/backing-claim.c` の `collect_metadata` が、UFS の metadata の extent（間接 block、512 byte の sector の番号）を disk の block として `canonical_range` に渡していた（段 4 は data の extent だけを換えていた）。partition の上では番号が disk の block の数を越えて EINVAL、小さな whole disk では範囲の中に収まり、違う範囲を守ったまま通っていた（下の速さの試験の 4096 の mkswap ok はこれ）。直した（commit c43982757）: 512 の単位を disk の block に換え、block に揃わなければ EIO。`include/kern/mount.h` の `file_metadata_extents` の comment に「512 byte の sector」と書いた。vmunix build warning 0。再試験は image を作り直して。
- 速さ（`sh plan/ws307/tests/p006-ufs-speed.sh build/ws307h-ssh/hdd-image.img`、新規、`build/ws307-test/p006-speed.oXIygf`、1 つの guest に 512 MiB の 512 と 4096 の NVMe、whole disk の native UFS）: 16 MiB の dd が 512 で 23 秒、4096 で 63 秒。1 MiB の書きと sync が 23 秒と 15 秒。どちらの mkswap も ok。UFS の書き込みは 512 でも約 0.7 MB/s で遅く（この Phase の前からの振る舞い）、4096 は大きな書き込みでさらに約 2.7 倍遅い（512 byte の journal の record の read-modify-write と見られる、未確認）。正しさの受け入れには関わらないので直していない（Q1 の判断）。
- 段 6 の残り: USB の 4096、p008 の 4096 の起動（guest の中で 4Kn の disk に ESP と root を作り、`BOOT_NVME_BLOCK_SIZE=4096` の boot-test.sh で起動、commit e799efd22 の道具）。

## 次の attempt への引き継ぎ（2026-10-29、P4）

- 段 5（mkfs の native UFS の fragment 4096 の profile、p005 の残り）の調べ: `userland/base/mkfs/ufs-format.c` の native の形は fragment 1 KiB・block 8 KiB（frag 8）に固定の数（`FORMAT_FRAGMENT`・`FORMAT_BLOCK`・`FORMAT_DATA_FRAGMENT` 144・`FORMAT_NATIVE_USED` 152、superblock の `sblkno` 64・`cblkno` 72・`iblkno` 80、cg の `IUSEDOFF` 168・`FREEOFF` 200、`FORMAT_IPG` 256、`fsbtodb` 1、`bshift` 13・`fshift` 10、inode の `DI_BLKSIZE` と `DI_BLOCKS` の 16 sector）。案: fragment 4 KiB・block 32 KiB（frag 8 のまま）の profile。fragment の番号は superblock 16（64 KiB）、cg 24、inode の表 32、data は表の後（block に揃える）。cg の大きさが block（32 KiB）以下なので 1 group は最大約 1 GiB、inode を 256／group のままにすると 1 GiB に 256 個で足りない。`ipg` を増やす（inode の bitmap の大きさで `FREEOFF` が動く、kernel は cg の header の offset を読むかを確かめる）か、group を小さくする（今の native と同じく fpg を下げて group を増やす）。kernel の UFS（`ufs.c` の superblock の検査、2639〜2647 付近）と loader の reader（`bootloader/common/ufs-read.c`、block 4096〜65536・fragment ≥ media block）が受ける形かを先に確かめる。`--check-size` の出力と `--sector-size=4096` も。
- 段 5 の後: `plan/ws307/tests/p006-4k-guest-steps.sh` に UFS（`mkfs -t ufs --profile=native` の 4096 の partition、mount、書き、再 mount、UFS の上の swap file、j3 の journal を通る書き）を足し、host で `bootloader/common/ufs-read.c` の host の試験の読み手で読む。USB の 4096（`-device usb-storage,…` に logical_block_size が効くか QEMU で確かめる）。
- その後 p008 の 4096 の段（loader の `volume-discovery.c` の `supported_fat_block_size` と BPB の sector を 4096 に、4Kn の ESP＋root の UFS で起動）。
