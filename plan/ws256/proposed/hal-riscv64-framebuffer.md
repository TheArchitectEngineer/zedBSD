<!-- awesome-plan project=zedbsd record=ws256-p012-proposed-hal-framebuffer -->

# WS256 の HAL の責務の差分の案: VisionFive 2 の framebuffer（承認の前は適用しない）

- 初版 2026-10-19 P1（q1270-i02、ws256-p012 改訂 2、レビュー 1 の M1）。**改訂 2026-10-19 P2（q1272-i01、ws256-p012 改訂 3、[レビュー 2](../design-peripherals-review-2.md) の MJ3・MJ4）**: `hal_get_arch_handoff` の実際の形、pitch・format の単位、返す BSP を直し、console を HAL に置く案を足した。
- guardrail「HAL の API の変更（hal.h・HAL の責務）は差分ごとの事前承認」に従い、ここに置くだけで `include/hal/`・`src/hal/` には触れていない。設計の全体は [design-peripherals.md](../design-peripherals.md) §3.1、承認の問いは §6.2 #29・文面は §6.3。

## 差分

### `include/hal/hal.h`

- **変えない**。使うのは既存の `void *hal_get_arch_handoff(const char *name)`（hal.h:1284-1292）: 1 引数、知らない・無い名前は **NULL**、返す object は HAL が持つ。

### 1. 名前付きの値（HAL の責務の追加）

- **返すのは VF2 の BSP（`src/hal/riscv64/bsp-visionfive2/`）だけ**。riscv64 の共通の `hal_get_arch_handoff`（`src/hal/riscv64/bootinfo.c`）は、知らない名前を board の内部の口（`riscv64_board_handoff(name)`、src/hal の中、hal.h 不変）に渡し、VF2 の BSP が次の名前を答える。virt の BSP は NULL を返す（virt の画面は HAL の ramfb の物で、U-Boot と loader の道で GOP があっても返さない）。
- 返す条件: loader の handoff（`bootloader/include/riscv64-handoff.h`）が**版 2 以上**で、framebuffer の field が有効（`framebuffer_size ≠ 0`、format が下の 2 つのどちらか、`size ≥ pitch × height`）。無効なら 6 つとも NULL（`kern_boot_framebuffer` の「全部あるか、全部無いか」に合う）。
- 各値は **HAL が持つ 64 bit の word への pointer**（rpi4・pcat と同じ形、init で 1 回埋めて以後変えない）。

| 名前 | 値 | 単位 |
| --- | --- | --- |
| `riscv64.framebuffer.phys` | framebuffer の物理 address（GOP の FrameBufferBase） | byte の address |
| `riscv64.framebuffer.size` | 窓の大きさ（GOP の FrameBufferSize） | byte |
| `riscv64.framebuffer.width` | 横 | pixel |
| `riscv64.framebuffer.height` | 縦 | pixel |
| `riscv64.framebuffer.pitch` | 1 行から次の行まで（GOP の PixelsPerScanLine × 4、loader が計算） | **byte**（pcat の pixel とは違う。名前ごとに文書で決める約束: include/kern/platform.h:80-88） |
| `riscv64.framebuffer.format` | 画素の形 | **`KERN_BOOT_FRAMEBUFFER_RGBX8888`（1、赤から）か `KERN_BOOT_FRAMEBUFFER_BGRX8888`（2、青から）**（include/kern/boot.h:56-61。loader の `video.c` が受けるのもこの 2 つだけ。rpi4 の firmware の画素の順の値とは違う） |

- kernel の読み手: `kern_boot_framebuffer("riscv64.framebuffer", &fb)`（`src/kern/pmem.c` の既存の関数）。p011 の `visionfive2.c` は読んで検査するだけで描かない。使い手は後の WS269・WS270 の画面の受け渡し（rpi4.c:202-221 と同じ形）。

### 2. VF2 の HAL が早期の text console を描く（HAL の責務の追加）

- bsp-virt の 80x25 の console（`cons.c`・`font.c`、`framebuffer.c` の cell の描画）を riscv64 の共通（`src/hal/riscv64/cons.c`・`font.c`・`fbcons.c`）に移し、board が `riscv64_fbcons_attach()` で framebuffer（kernel の address・width・height・pitch（byte）・format・`flush(start, length)`）を渡す。virt は ramfb を渡し（今と同じ画）、VF2 は handoff 版 2 の framebuffer を渡す。`hal_putc` が SBI の console と画面に出す約束は今の virt と同じ。
- cache: 既定は direct map で描き、描いた行の範囲を ccache の FLUSH64 で書き出す（rpi4 の WS044 p006 と同じ形）。p011 の試験で DC8200 が snoop すると分かれば flush を省き、flush で足りなければ system port の別名（物理 + 0x4_0000_0000、uncached）を device の窓に写して描く。どれも hal.h の既存の関数と src/hal の中の実装で、新しい API は無い。

### 承認の要らない付随の変更（src/hal の実装と loader → HAL の構造体、参考）

- `bootloader/include/riscv64-handoff.h` の版 2（framebuffer の field と、firmware が使う RAM の範囲 `reserved[]` 16 個。framebuffer の範囲を必ず含む）。loader → HAL の固有の構造体は guardrail「HAL と kernel・boot loader と HAL の受け渡し」の範囲。
- HAL が `reserved[]` を page の allocator から外し、framebuffer を uncached の別名で描く時は direct map からも外す（`page.c` の `reserved`、`space.c` の `nomap`）。

## 理由

- 既存の承認の名前は `riscv64.fdt`・`riscv64.initrd`・`riscv64.boot-hart` だけ（guardrail 2026-10-18）。framebuffer の名前と、VF2 で HAL が画面に描く事は新しい責務なので承認が要る（レビュー 1 M1、レビュー 2 MJ4）。
- 描画を HAL に置くと、HAL_FATAL と初期の message が画面に出て、kernel の console の多重化が要らない（レビュー 2 MJ3。kernel 側に汎用の framebuffer の text console は無い）。
- 名前と値の形を rpi4・pcat と揃えると、kernel の読み手は既存の `kern_boot_framebuffer` のまま。

## 承認の後に変える物（p011 (2) の code）

- `src/hal/riscv64/`: bootinfo.c の board への名前の委ね、console の共通化、cmain.c の handoff 版 2 の読み取り、page.c・space.c の予約。`src/hal/riscv64/bsp-visionfive2/`: 名前の 6 つ、`fbcons` の attach と flush。
- `bootloader/uefi/bootriscv64.c` と `riscv64-handoff.h`: GOP と予約の範囲。
- `src/kern/platform/visionfive2.c`: 読み手（検査だけ）。
