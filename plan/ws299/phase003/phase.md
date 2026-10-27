<!-- awesome-plan project=zedbsd record=ws299-p003 -->

# ws299-p003 NVMe の大きな転送（MDTS までの command、PRP list、bounce の写しを減らす）

- 親: [WS299](../ws.md)。Queue: q1617 / q1617-i01（担当 P1、2026-10-27）。前提: p001（測定）、p002（buffer cache の崖の直し、P1 の branch の e7b3f3211 に含む。main への merge は Q1 の判定待ち。基準の 384 MB/s は p002 の後の数）。
- Status: in-progress（実装・host・build・QEMU・5330 の測定は済み。目安の 1.5 倍に読みは届かず（1.3 倍）、原因は driver の外の read(2) の 64 KiB の staging。Q1 の判定待ち）
- Disposition: normal

## 目的と受け入れ

- 1 command が MDTS（BG5 は MDTS 9 = 2 MiB）と policy の上限まで運び、buffer cache の line（物理の page が分かっている）へは bounce を通さず直に DMA する。
- 受け入れ:
  1. host 試験: PRP の組み立て（page 列 → PRP1・PRP2・list、境界: 1 page・2 page・list の最大・端の offset）と command の分け方が PASS。
  2. build の warning 0（NVMe を持つ amd64（5330）・amd64-server・intelmac・pcat（i386、stack 16 KiB）・visionfive2、buf.c は全部に効くので rpi4・m68k-virt（stack 8 KiB）も）。
  3. QEMU の nvme で `boot-test.sh` の login と、大きな読み書きの cksum の一致（p002 と同じ手順、`plan/ws299/tests/qemu-grow-reboot.sh` を含む）。
  4. 5330 で p002 の後と前後を測る（`nvme-bench-5330.sh`、raw の連続の読み・書き、UFS の書き）。数は表に。この Phase だけで Debian の 95% は求めない（p004・p005 の後）。p003 の目標の目安: raw の連続の読みが p002 の後（384 MB/s）の 1.5 倍以上。UFS の書きは write-cached の mount では `transfer_run` を通らず line ごとの `disk_write_direct_context` なので、p003 では速くならない見込み（review L7）。direct と bounce の command の比を dmesg の `nvme: page-list commands` で記録する。

## 今の事実（2026-10-27、tree bb02f7b51＋p009、根拠は file:行）

- 読みの道: devfs `block_pread_data`（`src/kern/devfs.c:1625`）→ `disk_read` → `buf_read`（`src/kern/buf.c:678`）→ `transfer_run`（`buf.c:2344`、line は 1 page、1 run は `BUF_RUN_LINES` = `KERN_IO_BATCH_MAX`(64 KiB)/4 KiB = 16 line）→ `disk_transfer_progress_context` → `disk_transfer_direct`（`src/kern/disk.c:3924`、`d_max_transfer_blocks` で分けて BIO を 1 つずつ待つ）→ `nvme_disk_submit` → `nvme_io_pipeline`（`src/drivers/pci/pci-nvme.c:4806`）。
- 写しの回数（読み、run の時）: device → slot の bounce（4 KiB）→ `kern_memcpy` で caller の `data`（`pci-nvme.c` の pipeline の reap）→ `kern_memcpy` で line（`buf.c` の `transfer_run` の後半）→ caller の外へ（devfs の上）。書きは caller → line → bounce → device。
- driver: `NVME_IO_BOUNCE_SIZE` 4096（:38）、slot は queue の深さ − 1 = 63、slot ごとに 4 KiB の coherent の bounce（`nvme_io_dma_allocate`、:5310）。`maximum_transfer_bytes = drv_nvme_max_transfer_bytes(MDTS, page, NVME_IO_BOUNCE_SIZE)`（:5128）で 4 KiB に切る。command は PRP1 だけ（`drv_nvme_io_command`、`include/drivers/pci/pci-nvme-protocol.h:530`）、`drv_nvme_single_prp_transfer_valid` で 1 page に収まる事を確かめる。1 BIO の中で `NVME_IO_PIPELINE_DEPTH` 4 の command を同時に。
- controller の page は `KERN_PAGE_SIZE`（:1511、CC.MPS = 4 KiB）。
- line の物理: `struct buf` の `b_memory.paddr`（`include/kern/buf.h:43`、`buf.c:2253` で `hal_pmem_alloc` の page を `hal_pmem_to_kernel` で見る）。
- DMA の address: `drv_dma_alloc_coherent` の `device_address`。`drv_dma_map`（`src/drivers/generic/dma.c:579`）は device 自身の coherent の確保の中の address しか写せない。bus の address は `paddr + bus_offset`（`drv_dma_device_create_window`）。VMD の子は VMD の PCI device の DMA の device を共有する（`pci-vmd.c:385`）。
- partition の層は offset を足すだけ（`bio_admit`、`disk.c:3360` 付近）。`bio_admit` は `b_data == NULL` の転送を断る（:3377）。
- Linux（5330 の Debian）は `max_sectors_kb` 128 で 128 KiB の command、fio の seq 128k QD1 が 1123 MB/s（p001）。

## 設計

### D1. BIO の物理の page 列（block の層、`include/kern/disk.h`・`src/kern/disk.c`）

- `struct bio` に任意の欄を足す: `const uint64_t *b_pages`（物理 address の列、各 `KERN_PAGE_SIZE` の page の頭、転送の順）と `uint32_t b_page_count`。
- 契約: `b_pages != NULL` の BIO は `b_data == NULL` でよく、データの長さ（`b_block_count × block_size`）は `b_page_count × KERN_PAGE_SIZE` に等しい（page の途中から・途中までは無し）。page とその配列（`b_pages`）は caller が完了まで持ち、CPU は完了まで page に触らない（書きでは BIO の前に書き終える、読みでは完了の後に読む）。
- 道具: disk に能力の印 `DISK_PAGE_VECTOR`（`d_flags`）。driver が付ける。`disk_transfer_pages_context(disk, op, block, count, pages, page_count, completed, context)` を足す。leaf が印を持たなければ `EOPNOTSUPP` を返し、caller は今の道に戻る（呼ぶ前に `disk_page_vector_supported(disk)` で問うてもよい）。`d_max_transfer_blocks` で分ける時は page の境で分ける（block の数が page の倍数でない分け方は `EINVAL`）。
- `bio_admit`: `b_pages` のある BIO は `b_data` を要さない。`b_pages` と `b_data` の両方は断る（意味が二つになるため）。

### D2. 物理の page の DMA の address（`src/drivers/generic/dma.c`・`include/drivers/generic/dma.h`）

- 許可（2026-10-27 Q1 の技術判断）: 「src/drivers/generic/dma.c・include/drivers/generic/dma.h に drv_dma_physical_address を足してよい。条件: coherent でない device・limit の外では必ず失敗を返し、呼ぶ側は今までの bounce に戻る形（他の driver の振る舞いは変えない）、host 試験で境界（physical_limit・address_bits・segment_boundary）を確かめる。」「他の block の driver（USB の mass storage・AHCI など）が新しい欄を無視して今まで通り動くことも build と試験で確かめて。」

- `int drv_dma_physical_address(struct drv_dma_device *device, uint64_t paddr, size_t size, uint64_t *bus_address)` を足す。device が coherent で、`[paddr, paddr+size)` が `physical_limit` の内、`bus = paddr + bus_offset` が `address_bits` の内、`segment_boundary` を跨がず、`size ≤ max_segment_size` の時だけ 0 と bus の address を返す。それ以外は `EINVAL`（non-coherent の device は cache の手入れが要るので、この道を使わせない）。device の破棄の途中は `EBUSY`（既存の `device_operation_begin`）。
- HAL の API は変えない。

### D3. NVMe の command（`src/drivers/pci/pci-nvme.c`・`include/drivers/pci/pci-nvme-protocol.h`）

- slot ごとに PRP list の page（coherent、4 KiB = 512 entry）を 1 つ足す。63 slot で 252 KiB。
- slot の bounce を 4 KiB から `NVME_IO_BOUNCE_SIZE` 64 KiB（物理で連続、coherent）に（63 slot で約 4 MiB）。bounce の command も PRP list で運ぶ（連続なので list の entry は 4 KiB ずつの address）。page 列の無い BIO（superblock・`disk_read_direct`・async の payload など）が 64 KiB を 1 command で運べる。
- 1 command の上限 `command_bytes = min(MDTS, NVME_IO_MAX_COMMAND_BYTES)`。`NVME_IO_MAX_COMMAND_BYTES` は 2 MiB（MDTS 9 の BG5 で MDTS と同じ）。PRP の数の上限: 頭の page（PRP1）＋ list の 512 entry。list の chain は作らない（512 entry で 2 MiB＋1 page まで足りる）。`maximum_transfer_bytes` の計算から bounce の大きさを外し（bounce は page 列の無い BIO の上限として別に効かせる）、`drv_nvme_max_transfer_bytes(MDTS, page, NVME_IO_MAX_COMMAND_BYTES)` に。
- PRP の組み立ては host で試せる純粋な関数（protocol の header の `static inline`）: 入力は bus の address の page 列（各 page の頭、page 内の offset は 0）と数、出力は PRP1・PRP2 と list の entry。規則（NVMe 2.0 Base §4.1.1 の PRP、公開の仕様）: 1 page は PRP1 だけ、2 page は PRP1・PRP2、3 page 以上は PRP2 = list の bus の address、list に 2 枚目以降。
- page 列の BIO の command: BIO の page 列を `command_bytes / page` ずつに切り、各 page を D2 で bus の address にし、PRP を組んで post。D2 が断った page を含む command は、その command だけ bounce で運ぶ（page を `hal_pmem_to_kernel` で見て、書きは bounce へ写してから、読みは完了の後に page へ写す。bounce の 64 KiB を超える command は bounce の大きさに切る）。
- 読みの完了で写さない（page 列の時）。bounce の時は今どおり写す。
- `disk` に `DISK_PAGE_VECTOR` を付けるのは、controller の page が `KERN_PAGE_SIZE` と等しく、DMA の device が coherent の時だけ。
- pipeline の深さ（4）、queue の数・深さ、割り込みは p004 で。

### D4. buffer cache の run（`src/kern/buf.c`）

- `transfer_run`: leaf が `DISK_PAGE_VECTOR` を持てば、line の `b_memory.paddr` を page 列にして `disk_transfer_pages_context` で運ぶ。書きは今どおり caller → line に写してから（device は line から読む）。読みは完了した prefix の line から caller の `data` へ写す（今の向きの逆）。印が無い leaf は今の道（`data` を BIO に）。
- run の上限を、page 列の時だけ 64 line（256 KiB）に上げる（`BUF_RUN_PAGE_LINES`）。64 line の帳簿（line・generation・page の列、約 1.5 KiB）は stack に置かず heap（`struct buf_run_scratch`）に取り、取れなければ今の 16 line の stack の道（review M3）。256 KiB より大きな run・readahead は p005。

### D5. 変えない物

- HAL（`include/hal/hal.h`）、UAPI、queue・割り込み（p004）、readahead（p005）、HMB（p006）。他の block の driver（AHCI・USB・SD）は印を付けないので今の道のまま。


## design-reviewer の指摘と決定（2026-10-27、review は HEAD e7b3f3211 と作業中の差分を読んだ）

| 指摘 | 決定 |
| --- | --- |
| S1 失敗の経路で DMA が止まる前に page が返る（bounce は slot が持つので安全だった） | slot に `dma_uncertain`（post 中に `nvme_io_fail_all_locked` で落とされた）を、controller に `io_dma_quiesce_count`（`nvme_io_lifecycles_quiesce`、つまり disable・bus master の停止・IRQ の drain の後でだけ進む）を足す。page 列の command が `dma_uncertain` で終わった BIO は、slot を返して recovery を回した後、数が進むまで完了しない（`nvme_io_wait_dma_quiesced`）。recovery の担当がいなければ待つ側が取る。DMA を止められない隔離（`dma_unsafe`）では BIO は待ち続け、line は busy のまま（bounce の memory を手放さないのと同じ扱い。(a) を選ぶ） |
| S2 amd64 の PCI の DMA の device は 32 bit（`pci-pcat.c:92-96`）、4 GiB より上の line は直に届かない | 届かない page の所で command を切る（直に届く頭の部分は直に、頭が届かなければ届く page の手前まで bounce）。direct と bounce の command の数を数え 4096 command ごとに dmesg に出す（UAPI の io-stats は変えない）。5330 の比を測ってから、NVMe だけ 64 bit の DMA にする案（generic の PCI の変更、Q1 の許可が要る）を判断する |
| M1 `d_max_transfer_blocks` が 64 KiB | NVMe は 1 MiB（`NVME_IO_MAX_BIO_BYTES`）に。page 列の分割は page の境で。`bio_async_prepare` は KERN_IO_BATCH_MAX が別に効く、swap は影響なし（review の確かめ） |
| M2 build の一覧 | 受け入れ 2 を直した |
| M3 stack | run の帳簿（64 line）は heap（`struct buf_run_scratch`、取れなければ今の 16 line の stack の道）、PRP の bus の address の列は slot ごとの heap の配列（`page_bus`、512 entry） |
| M4 64 KiB の bounce の確保の失敗 | 全部の slot に取れなければ、全部 1 page の bounce でやり直す（log を出す） |
| M5 設計の前に範囲の外を変えた | D2 は Q1 の許可（上）。差分の決定はこの表に書き戻した |
| M6 EOPNOTSUPP で戻る契約 | `disk_transfer_pages_context` は何も送る前だけ EOPNOTSUPP を返し、送った後の EOPNOTSUPP は EIO にする。buf.c は `completed == 0` かつ EOPNOTSUPP の時だけ今の道へ。block が page より大きい disk は page 列の道を使わない |
| M7 試験 | `plan/ws299/tests/qemu-nvme-shapes.sh`: MDTS 9（2 MiB の command、511 entry の list）と MDTS 3（32 KiB で多く分かれる）、8 GiB の guest（line が 4 GiB の上下に混ざる）。4096 byte の LBA は driver が扱わない（`drv_nvme_identify_namespace_validate` が 512 だけ）ので対象外。失敗の注入は行わない（未実施） |
| L1 MDTS の単位は CAP.MPSMIN | code は CC.MPS（= `KERN_PAGE_SIZE`、:1511）を渡す。MPSMIN が 4 KiB の device では同じ値。MPSMIN が大きい device は今の driver が `KERN_PAGE_SIZE` の page を使えないので別の問題 |
| L2 page の大きさの条件 | 残す（`KERN_PAGE_SIZE` の page を前提にする事の明示）。実際の判定は coherent と D2 |
| L3 `hal_pmem_to_kernel` | driver は `kern_pmem_to_kernel` を使う |
| L4 写しの粒度 | S2 と同じく page の所で切る |
| L5 記録 | 前提と表を直した |
| L6 出典 | MDTS は Identify Controller の byte 77（NVMe Base 2.0、節の番号は書かない）。PRP の helper の上に複数行の出典の注記は付けない（仕様の規則の要約を関数の説明に書いた。code・値の引用は無い） |
| L7 UFS の書き | 受け入れ 4 に書いた |
| L8 `pci-nvme.c` は WS300 と共有 | WS299 → WS300 を同じ担当（P1）が続けて持つ（pm-plan のとおり） |
| L9 page の配列の寿命 | D1 の契約に追記（配列も完了まで持つ） |
| L10 run の短縮 | `reference_line` が 2 line 以上取れた後に失敗したら、そこまでに縮める |

## 実施（2026-10-27）

| 物 | 内容 |
| --- | --- |
| `include/drivers/generic/dma-range.h`（新）・`dma.h`・`dma.c` | `drv_dma_range_bus_address`（純粋な判定）と `drv_dma_physical_address`（coherent の device だけ） |
| `include/drivers/pci/pci-nvme-protocol.h` | `drv_nvme_prp_fill` |
| `include/kern/disk.h`・`src/kern/disk.c` | `DISK_PAGE_VECTOR`、`struct bio` の `b_pages`・`b_page_count`、`bio_admit` の検査、`disk_page_vector_supported`、`disk_transfer_pages_context` |
| `src/kern/buf.c` | `transfer_run` を wrapper と `transfer_run_lines` に。page 列の時 64 line（256 KiB）、読みは line から caller へ写す |
| `src/drivers/pci/pci-nvme.c` | slot の PRP list の page・`page_bus`・64 KiB の bounce（失敗で 1 page）、`nvme_io_build_command`、page 列の pipeline、S1 の待ち、command の数の log、`d_max_transfer_blocks` 1 MiB、`DISK_PAGE_VECTOR` |

### 確かめ

| 確かめ | command | 結果 |
| --- | --- | --- |
| host | `cc -std=c99 -Wall -Wextra -Werror -I include -o build/tmp/nvme-prp-test plan/ws299/tests/host/nvme-prp-test.c && build/tmp/nvme-prp-test`、同じく `dma-range-test.c`、既存の `plan/ws004/tests/nvme-io-test.c` | `nvme-prp-test: PASS`、`dma-range-test: PASS`、`HW-T20 NVMe I/O helper: PASS` |
| build | `make ZEDBSD_CONFIG=<config> BUILD=build/p1-p003-<name> vmunix`（config-5330・amd64-server・intelmac・pcat・visionfive2・rpi4・m68k-virt） | 全部 rc 0、warning 0 |
| QEMU 起動 | `OUTPUT=build/p1-qemu3-boot plan/tools/boot-test.sh build/p1-qemu3/hdd-image.img`（`plan/tools/posix/config-amd64-serial.mk`） | exit 0、画面に login: |
| QEMU 読み書き | `amd64-serial.sh`: raw 1500 MiB の読み、300 MiB の乱数の file → raw 2000 MiB で cache を一巡 → cksum、64k で写す、512 byte の読み | cksum 一致、512 byte の読みは前の kernel と同じ値。raw 1500 MiB は 4.80 秒（前の kernel は 18.84 秒、QEMU の値で実機の比ではない） |
| QEMU の形 | `sh plan/ws299/tests/qemu-nvme-shapes.sh build/p1-qemu3/hdd-image.img` | `qemu-nvme-shapes: PASS (5 checksums equal)`（MDTS 9 と 3） |
| QEMU 大きな file と reboot | `sh plan/ws299/tests/qemu-grow-reboot.sh build/p1-qemu3/hdd-image.img 3` | `PASS reset after 1 s`、cksum 一致 |

### 64 bit の DMA（2026-10-27、Q1 の判断 (A)）

- Q1（技術判断）「(A) で可。drv_dma_physical_address に device 自身の address の幅を引数で渡し、NVMe は 64 を渡す。bus_offset・platform の physical_limit・segment_boundary・coherent の検査は残し、それに外れる時は必ず失敗を返して bounce に戻る（前の条件はこの形に改める）。pci-pcat の 32 bit の既定は残し他の driver は変えない。host 試験に「device 64 bit・platform の limit の内外」を足して。」
- 実施: `drv_dma_physical_address(device, paddr, size, device_address_bits, &bus)`（0 は DMA の device の幅のまま）、NVMe は `NVME_DMA_ADDRESS_BITS` 64。`dma-range-test.c` に 32 bit の既定で断られる 4 GiB の上の page が 64 bit で通る事、platform の limit の内・端・外、bus offset を足した。
- 確かめ: `dma-range-test: PASS`・`nvme-prp-test: PASS`、build（config-5330・pcat・visionfive2・intelmac）rc 0・warning 0、QEMU（image `build/p1-qemu4`）: boot-test PASS、`qemu-nvme-shapes.sh` PASS（`nvme: page-list commands direct=4096 bounced=0`）、`qemu-grow-reboot.sh` PASS（reset 2 秒）。

### 5330 の前後（MB/s、`nvme-bench-5330.sh zedbsd zb-p003b …/vmunix read write`、kernel sha256 `5a76273812a303f6…`、要約 `plan/ws299/evidence/zb-p003b-summary.md`。p003a は 32 bit の bounce の時）

| 測り方 | p002 の後 | p003a | p003b（64 bit） | Debian（同じ条件） | 比 |
| --- | --- | --- | --- | --- | --- |
| raw の連続の読み 256 MiB ×16 | 363〜384 | 426〜655（多くは約 460） | 463〜688（多くは約 490） | page cache 経由 1.1〜1.2 GB/s（b-chunk の冷えた段） | 約 43% |
| raw の書き 256 MiB ×4 | 597〜655 | 639〜813 | 767〜959 | 1.6 GB/s（b-w1m） | 約 55% |
| UFS の file の書き | 433〜479 | 571〜671 | 455〜488 | ext4 1.6 GB/s | 約 30% |
| page 列の command | — | direct 2% | direct 100%（bounced 0） | — | — |

- 読みが伸びない原因（数）: `run_rd`・`drv_rd` が 256 MiB に 4096、つまり 1 run・1 BIO が 64 KiB のまま。dd の 1 MiB の read(2) は `sys_read_call` が block device を `SYSCALL_STREAM_CHUNK`（64 KiB、`src/kern/syscall.c:87`）の staging に分けて 16 回 `block_pread` を呼ぶ（`syscall.c` の `syscall_io_buffer`）。64 KiB を QD1 で同期に読む上限（Debian の `d-bs64k` 697 MB/s）に近い。driver の command の大きさ（p003）はこれ以上効かない。
- 次の手（p004・p005 の範囲）: block device の read(2) の staging を大きくする（syscall.c、担当の範囲の外なので Q1 に聞く）、buffer cache の run の先読み・非同期（p004・p005）。
- UFS の書きは write-cached の mount で `transfer_run` を通らない（review L7）ので p003 では変わらない（値の揺れの内）。

## 出典

- NVMe Base Specification 2.0（公開）§4.1.1 PRP、§5.15.2.2 MDTS（Identify Controller byte 77）。PRP list の chain と entry の規則は仕様から。Linux・FreeBSD の nvme の code は読まない（値も使わない）。

## 試験の計画

- host: `plan/ws299/tests/host/nvme-prp-test.c`（PRP の組み立て、command の分け方、D2 の範囲の判定は dma.c を host で build できなければ純粋な判定の関数に分けて試す）。
- build: `config-5330.mk`・`config/ci/config-{amd64,rpi4,rpi5,visionfive2}.mk` の vmunix。
- QEMU: p002 と同じ（`boot-test.sh` uefi-nvme、`amd64-serial.sh` の読み書き cksum、`qemu-grow-reboot.sh`）。
- 5330: `nvme-bench-5330.sh zedbsd zb-p003 VMUNIX read write`、`5330-run.sh --nvme-kernel`。

## 判断が要る点

- D2 は `src/drivers/generic/dma.c`（担当の範囲の外）を変える。Q1 の許可、または D2 を NVMe の driver の中だけで済ます案（`drv_dma_device` の bus_offset を読む口が無いので、identity の DMA の device の時だけ `paddr` をそのまま使う、を driver が判定する口が要る）。推しは D2（generic の小さな口）。
