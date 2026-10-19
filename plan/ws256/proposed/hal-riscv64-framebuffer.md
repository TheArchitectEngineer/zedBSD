<!-- awesome-plan project=zedbsd record=ws256-p012-proposed-hal-framebuffer -->

# WS256 の HAL の責務の差分の案: riscv64 の framebuffer の名前付き handoff（承認の前は適用しない）

2026-10-19 P1（q1270-i02、ws256-p012 改訂 2、レビュー 1 の M1）。guardrail「HAL の API の変更は差分ごとの事前承認」に従い、ここに置くだけで `include/hal/`・`src/hal/` には触れていない。

## 差分

- `include/hal/hal.h`: **変えない**（`hal_get_arch_handoff(name, value)` の契約の中の名前を 1 組足すだけ）。
- riscv64 の HAL（`src/hal/riscv64/`、board 共通）が、ZBL6 の handoff の framebuffer の field（loader が UEFI の GOP から入れた物。p004 の loader の riscv64 版）が有効な時だけ、次の名前を返す。無効なら全部 `HAL_ERR_NOT_FOUND`（今の rpi4・pcat と同じ約束）。

| 名前 | 値 |
| --- | --- |
| `riscv64.framebuffer.phys` | framebuffer の物理 address |
| `riscv64.framebuffer.size` | byte の大きさ |
| `riscv64.framebuffer.width` / `.height` / `.pitch` | pixel・byte |
| `riscv64.framebuffer.format` | rpi4・pcat と同じ format の値（`kern_boot_framebuffer` の `format`） |

- kernel は `kern_boot_framebuffer("riscv64.framebuffer", &fb)`（`src/kern/pmem.c` の既存の関数）で読み、kernel 側の framebuffer の console で描く（HAL の BSP は VF2 で描かない。design.md の「VF2 の BSP に framebuffer は無い」を保つ）。virt は今までどおり HAL の ramfb の console（この名前は virt で返さない: ramfb は HAL が持つ）。
- 描く側の cache の扱いは design-peripherals.md §3.1 の二案（(α) system port の別名へ写す、(β) 描画の後に `hal_dcache_clean_range`）。どちらも hal.h の既存の関数で、新しい API は無い。

## 理由

- 既存の承認の名前は `riscv64.fdt`・`riscv64.initrd`・`riscv64.boot-hart` だけ（guardrail 2026-10-18）。framebuffer の名前は新しい責務なので承認が要る（レビュー 1 M1）。
- `rpi4.framebuffer.*`・`pcat.framebuffer.*` と同じ形にすると kernel の読み手が既存の関数のまま。

## 承認の後に変える物（p011 の code の Phase）

- `src/hal/riscv64/` の handoff の表に 6 行、kernel の `src/kern/platform/visionfive2.c` に読み手と console の選択。
