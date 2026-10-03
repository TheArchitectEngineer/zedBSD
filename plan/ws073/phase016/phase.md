<!-- awesome-plan project=zedbsd record=ws073p016 -->

# ws073-p016: FAT の readdir の位置（`rm -r`）と FAT の inode の pool の枯渇

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bugs: [BUG-074](../../bugs/BUG-074.md)、[BUG-076](../../bugs/BUG-076.md)（どちらも ws073-p015・p016 の試験で発見、既存）

## 目的と受け入れ

1. BUG-074: FAT の directory を読む途中で entry を消しても、消していない entry がちょうど 1 回ずつ返り、`rm -r` が成功する。
2. BUG-076: 空きのある FAT に file を数百作れる。開いている・dirty・claim のある（swap・loop）・mount の root の inode は追い出さない。
3. tmpfs の多数の file（BUG-029、隣の code）の回帰が無い。boot test。

## 原因と修正

- BUG-074: `fat_raw_readdir()`（`src/drivers/fs/fat.c`）の位置が「見えた entry の数」で、`f_offset++` だった。unlink で 1 つ前の entry が
  消えると次の entry の番号が 1 つ下がり、1 つ飛ぶ。位置を directory の 32 byte の record の番号にした: `fat_raw_readdir(state, path, start,
  &entry, &next)` は `start` 以降の最初の見える entry を返し、`next` にその短い名前の record の次を返す。`fat_readdir_unlocked()` は
  `f_offset = next`。消した entry は 0xE5 の record として場所に残るので、後ろの entry の位置は変わらない（1 回の読みあたり O(n) だった走査も
  続きからになる）。`f_offset` が 32 bit の外なら終わり。
- BUG-076: `fat.c` の inode の pool は全 FAT mount で 256 個に固定で、汎用の inode cache が参照の無い clean な inode を持ち続け、
  `inode_storage_take()`（`src/kern/inode.c`）は filesystem の `alloc_inode` が NULL だと諦めていた。`evict_filesystem_inode(type)` を足し、
  filesystem の storage が満ちたら同じ型の inode を 1 つ追い出して再試行する（最大 4 回）。追い出すのは cache だけが持つ（`i_refs == 1`）inode で、
  `INODE_DIRTY`・`INODE_ROOT`・`INODE_DEAD`（unlink 済みで filesystem が storage をまだ返す必要のある orphan）・`INODE_SWAPFILE`・
  `INODE_LOOPFILE` を持つものは追い出さない。開いている file と swap・loop の file は参照を持つので対象にならない。

## 検証（QEMU、KVM、NVMe、amd64。実機は未実施）

- build: guest の vmunix（`-Werror`）、lean native の disk-image。規約: `tests/style-diff.py`（fat.c・inode.c）0。
- [tests/fat-rmr.sh](../tests/fat-rmr.sh)（main の測定用 image に vmunix を差し替え、[tests/p015-native.sh](../tests/p015-native.sh) の FAT の disk）を
  ESP（512 B cluster）、512 B と 4 KiB cluster の FAT32 で、それぞれ 9 件 PASS: 6 個の directory の `rm -r`、短い名前と長い名前の 300 個の
  directory の `ls`（300 個、重複なし）と `find`（300）、300 個と部分木の `rm -r`、手で 1/4 を消した後の `ls` が残りの 30 個、`rm -r`、sync。
  修正の前は、6 個の `rm -r` が "Directory not empty"（f2・f4・f6 が残る）、300 個の作成が 252 個目で ENOSPC。
- claim のある FAT で追い出しの圧力: 2 KiB cluster の FAT で 4 MiB の swap file を有効にしたまま 400 個の file を作り（`ls` で 400）、swap file への
  追記と rm は EBUSY のまま、`rm -r`・swapoff・rm・umount が通る。停止の後の host の `fsck.fat -n` で 3 つの FAT とも clean。
- tmpfs（BUG-029 の回帰、[tests/tmpfs-many.sh](../tests/tmpfs-many.sh)）: 10000 個 × 3 回、byte の quota、全 PASS（failures 0）。
- boot test（lean native、UEFI・NVMe）: PASS（`build/ws073-p016/boot-native/login.png`）。
- 未実施: pcat・pc98・rpi4（amd64 だけの方針）、実機。

## 残り

- [BUG-075](../../bugs/BUG-075.md)（読んだ後に消した file の chain が inode が消えるまで解放されない）は別。追い出しは `INODE_DEAD` の orphan を
  扱わないので、この Phase は BUG-075 を変えない。
