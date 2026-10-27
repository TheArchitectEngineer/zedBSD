<!-- awesome-plan project=zedbsd record=ws299-p010 -->

# ws299-p010 block device の O_DIRECT

- 親: [WS299](../ws.md)。Queue: q1640 / q1640-i01（担当 P1）。番号は P1 の仮（Q1 が表と依存を決める）。
- Status: planning（2026-10-28 P1: 設計（kernel-and-driver-designer の案、P1 が決めた点を併記）、design-reviewer へ）
- 出典: 2026-10-28 ユーザー（Q1 経由）「raw の冷えた読み 4 GiBは、zedBSDではpage cache経由なのか、directなのか。zedBSDでもどちらの経路でも読めるなら、両方でLinuxと同等以上の性能にチューニングしたいです。」、P1 の案 B に対しユーザーのクリック「O_DIRECT を UAPI に足す」。Q1: block device の open に O_DIRECT（Linux と同じ意味: cache を通らず、重なる dirty の line は先に書き戻して無効にする）、揃わない offset・長さの扱いも Linux に合わせて決めて記録、regular file は EINVAL か無視かを決めて記録、受け入れに direct の読み・書き（Debian の direct 2.1 GB/s 以上、書きも Debian の direct と比べる）。

## 今の事実

- 普通の open の raw の読み書きは buffer cache を通る（`src/kern/devfs.c` block_pread_data:1628 → disk_read → buf_read と p004 の先読み。書きは p005 の disk_write_delayed）。cache を通らない道は BLKRESERVE の排他の claim の open だけ（disk_read_direct、devfs.c:1677・1706）。UAPI に O_DIRECT は無い（`include/uapi/fcntl.h`:68-83）。
- syscall の層は user の buffer を先に pin し（`sys_read_call` syscall.c:2932、write :3037、vector :4825）、1 MiB の kern_malloc の bounce に読んでから写す（:2838-2878）。page 列でない BIO では NVMe の driver も bounce する。

## 設計

### UAPI と libc

- `O_DIRECT 0x100000`（fcntl.h、空いている次の bit）。libc は flag をそのまま渡すので変えない（`include/libc/fcntl.h`:15、`userland/base/libc/posix.c`:655・686・834-838）。

### 意味（P1 の決定）

- block device だけが受ける。regular file・directory・chr・FIFO は open で EINVAL（黙って無視すると cache を避けたつもりの測定やアプリが誤る。EINVAL ならアプリが fallback できる）。`/dev/fd/N` は元の description に無い O_DIRECT を求めたら EINVAL。
- F_SETFL の mask を `O_APPEND|O_NONBLOCK|O_DIRECT` に広げ、block 以外で O_DIRECT なら EINVAL。F_GETFL は変えない。
- offset と長さは logical block（`d_block_size`）の倍数でなければ EINVAL（Linux と同じ）。長さ 0 は 0。device の終わりは今の `devfs_block_io_range_prepare` のまま（読みは切り詰め、書きは ENOSPC）。
- buffer の address の揃いは求めない（P1 の決定。Linux は logical block の揃いを求めるが、zedBSD は揃わない buffer を staging で扱えるので、より緩い。揃った buffer だけが zero-copy になる）。
- dd の iflag/oflag=direct は今回は足さない（試験は道具 `plan/ws299/tests/directio.c`）。dd に足すかは Q1 へ。

### 経路

- `struct file_ops` に `pdirect(file, write, const struct uaccess_pin *, size_t pin_off, size_t len, off_t off)`、file.c に `file_io_transfer_pinned`。syscall の 4 箇所（read・write・positional・vector）は O_DIRECT の block の時だけ bounce の loop を飛ばしてこれを呼ぶ。
- A（zero-copy）: pin の先頭が page の頭、範囲の page が全部 `VMSPACE_PINNED_PRIVATE`、leaf が DISK_PAGE_VECTOR、長さが page の倍数 → paddr の配列で page 列の DMA。OBJECT（MAP_SHARED）・DEVICE の page は外す。
- B（staging）: kern_pmem の page の 1 MiB × 4 の pool（初めて使う時に確保して保つ）に page 列で DMA し、`copyout_pinned`・`copyin_pinned` で 1 回だけ写す。4 KiB の倍数でない端と page 列を持たない disk は `disk_transfer_direct`。kernel の buffer を渡す呼び手も O_DIRECT の時は B。
- disk.c: `disk_direct_enter`・`leave`（`disk_cache_enter` と同じ受け入れ: reload・admin の除外）、claim を引数に取る page 列の転送（BLKRESERVE の fd が自分の claim と衝突しないように）。

### cache との整合

- buf.c に `buf_direct_prepare(disk, blk, cnt, evict)`（範囲の line を hash で引き、busy・io_inflight は待ち、dirty は書き戻し、書きの時だけ evict）と `buf_direct_finish`（書いた後に clean の line をもう一度 evict）。leaf に cache の line が 0 なら素通り。`buf_invalidate` は disk 全体の sync と O(cache) の walk・使用中で EBUSY なので使わない。
- 読みは prepare（evict なし）、書きは `backing_mutation_begin_disk` → prepare(evict) → DMA → finish。p005 の遅らせた raw の書きの dirty は prepare が書き戻す。direct の道は `buf_readahead_observe` を呼ばない。buffered と direct の fd を同時に同じ範囲に使った時の結果は決めない（Linux と同じ）。

### 誤り・signal

- BIO の確かめた先頭（`completed`）を返し、0 なら -errno。BIO 1 本は待ち終えるまで割り込まない（DMA が止まるまで page を手放さない）。1 MiB を越える時は BIO の間で signal を見て短い数を返す。

## 受け入れ

1. host 試験（揃い・道の選び A/B/端の純関数、prepare）。
2. QEMU（NVMe 512 B・4 KiB の sector、virtio-blk）: `directio.c` で書いて読んで cksum、buffered の書きの直後の direct の読み、cache に載った範囲を direct で書いた後の buffered の読み、EINVAL（offset・長さ・regular file・F_SETFL）、揃わない buffer・MAP_SHARED の buffer での正しさ、fork と並べた読み。io_stats に A・B の道の数。
3. 5330: direct の読み 4 GiB が Debian の `dd iflag=direct bs=1M`（2.1 GB/s）以上、direct の書きが Debian の `oflag=direct`（同じ LBA・大きさ）の 95% 以上。乱読み 4 KiB の QD1・QD32 も direct で Debian の fio direct と比べる。
4. build の warning 0。

## 危険

- 1 MiB ごとの `uaccess_pin` の費用は未測定。O_DIRECT を `#ifdef` で使う移植物が regular file で EINVAL を受ける。HAL の API は変えない。

## レビュー（2026-10-28、design-reviewer）と反映の方針（実装の時に守る）

- H1 → 「block だけ」の検査は open で inode を決めた直後（`file.c` の作成・O_TRUNC の切り詰めより前、:340 の付近）に置く。`i_descriptor_alias`（`/dev/fd/N`）だけは除き、`file_substitute_descriptor` で元の description の O_DIRECT と比べる。flag の mask は :256 だけを広げる。
- M1 → open・F_SETFL・syscall の振り分けは `i_type` でなく `f_ops->pdirect != NULL` で判定する（tmpfs の mknod の block node は `i_fop` が NULL）。
- M2 → 書きは、journal の pin か参照の残る line に重なる時、短く待ってから EBUSY。読みは `buf_writeback_cached` と同じ規則。finish は誤りの道でも必ず走らせ、io_inflight の line も待ってから追い出す（先読みの worker が prepare の後に line を入れる窓）。
- M3 → O_SYNC・O_DSYNC の direct の書きは device の flush だけ（disk 全体の `buf_sync` をしない）。試験で確かめる。
- M4 → BLKRESERVE の claim の fd は今の block_pread と同じく `disk_admin_io_begin` を使い、prepare をしない。
- M5 → 1 MiB を越える要求は BIO を 2 本重ねる。受け入れに bs=4M を足す。B（staging）の道は受け入れの速さの対象から外す。
- M6 → 5330 の direct の書きは p7（`zedBSD-nvme-bench`、PARTLABEL と PARTUUID を確かめる既存の `p7_check`）だけ。
- 低: Linux 6.x の buffer の揃いは dma_alignment（STATX_DIOALIGN）で、上の「logical block の揃いを求める」は不正確。device の終わりを越える書きは Linux が短い数、zedBSD は ENOSPC（違いとして記録）。io_stats に道の数を足すなら末尾に足し VERSION を上げ userland の sysctl も直す。mutation の slot（上限 64）を書き 1 回で 2〜3 個使う。staging の pool の確保の失敗・全部使用中の fallback を書く。ws.md の表と依存は Q1。`directio.c` が O_DIRECT を見るには sysroot の header の更新が要るかもしれない（toolchain の範囲、Q1 の許可の要否を確かめる）。libc の O_RSYNC の comment（`include/libc/fcntl.h`:17）を直す。
- 足す試験: 既存の regular file に O_TRUNC|O_DIRECT で中身が残る、tmpfs の mknod の node、`/dev/fd/N`、root を mount したままの whole disk の direct、signal の短い数が block の倍数、転送中の munmap、誤りの注入、O_DSYNC の flush、BLKRESERVE の fd、F_SETFL の切り替え、512 B の sector の 4 KiB に揃わない offset、coherent でない arch（arm64・riscv64 の QEMU）の B の道、MAP_SHARED|MAP_ANONYMOUS の buffer。

## 再開点（2026-10-28、P1）

- 設計と review は済み、実装は未着手（P1 の context の都合で次の起動へ）。順: a 意味と正しさ（UAPI・open・F_SETFL・揃い・B の道・prepare/finish、host と QEMU）→ b 速さ（pdirect・A の道・pool・BIO 2 本）→ 5330。
