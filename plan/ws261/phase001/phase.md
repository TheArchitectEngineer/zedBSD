<!-- awesome-plan project=zedbsd record=ws261-p001 -->

# ws261-p001 boot.h の PC-98・RPi の構造体を除く

Status: in-progress（2026-10-18 q1254-i01、P3）

- 親: [WS261](../ws.md)。Queue: q1254-i01。
- 規約: [Guardrail](../../guardrail.md)「HAL と kernel・boot loader と HAL の受け渡し」（2026-10-18 ユーザー）。

## 洗い出し（2026-10-18 P3、`git grep`、plan/history を除く）

| 名前 | 使う所 |
| --- | --- |
| `KERN_HANDOFF_VERSION_PC98` | `src/hal/i386/bsp-pc98/boot.c`、`src/hal/x86/boot-parameters.c`（`x86_pc98_handoff_classify`、amd64 の HAL にも build される）、kernel の `src/kern/entry.c`・`src/kern/platform/pc98.c`・`src/kern/vfs.c`（照合と log） |
| `KERN_PC98_HANDOFF_COMMON_SIZE`・`KERN_PC98_PARAMETER_RECORD_OFFSET`・`KERN_PC98_PARAMETER_HANDOFF_SIZE` | `bootloader/pc98/bootzbsd.S`、`bootloader/tests/payload32-pc98.S`、`src/hal/x86/boot-parameters.c` |
| `struct kern_pc98_parameter_handoff` | `src/hal/i386/bsp-pc98/boot.c` |
| `struct rpi4_boot_handoff`・`KERN_RPI4_HANDOFF_MAGIC` | `src/hal/arm64/bsp-rpi4/boot.c`（作って `kernel_entry` へ）、`src/kern/platform/rpi4.c`（cast して `fdt_phys`・`sdhci_phys`・framebuffer を読む） |

- ws.md の対象の列にある arm64 の cmain・bsp・page・smp・space、bsp-rpi4 の fdt・gic・cons、`src/drivers/platform/rpi4/`（bt・audio・ethernet・sdhci）は構造体を使っていない（`rpi4_boot_info()` か引数の fdt で受ける）。cmain.c は `rpi4_kernel_handoff()` を `kernel_entry` に渡すだけ。
- `bootloader/pc98/{lba2,disk-ipl}.S` の `KERN_PC98_IPL_DIAGNOSTIC` は boot.h の物ではない（build の define）。範囲外。
- 範囲外で触らない: `KERN_SUN4U_*`・`struct kern_sun4u_*`（sparcv9）、`KERN_X68K_*`（同じ「XXX: To be removed」の印）。扱いはユーザーに聞く（ws.md）。

## 置き換えの形

### PC-98（2026-10-18 Q1 了承）

- boot loader → HAL の定義は HAL の中の `src/hal/i386/bsp-pc98/handoff.h` へ: `PC98_HANDOFF_VERSION_NATIVE`（2、loader の assembler が読むので接尾辞なし）、`PC98_HANDOFF_COMMON_SIZE`、`PC98_PARAMETER_RECORD_OFFSET`、`PC98_PARAMETER_HANDOFF_SIZE`、`struct pc98_parameter_handoff`。`bootzbsd.S`・`payload32-pc98.S` はこれを include する（`platform/pc98/vmunix.mk` の依存の列にも足した、Q1 了承）。
- `x86_pc98_handoff_classify` は PC-98 専用なので `src/hal/x86/` から `bsp-pc98/boot.c` の static `classify_handoff` へ移した。
- HAL → kernel: HAL は kernel への handoff を常に共通の version（`KERN_HANDOFF_VERSION_MULTIBOOT`）と共通の size（24）にする。loader の形の区別は HAL に留め、kernel は汎用の field で読む: native は `boot_partition_scheme = KERN_PARTITION_SCHEME_LBA`・index 0・`boot_partition_lba`、Multiboot は MBR の 1〜4。
  - `entry.c`: 受け付けから PC98 の version を外した。
  - `platform/pc98.c`: version は共通の物だけ、scheme が MBR なら index 1〜4、MBR・LBA 以外の scheme は拒む（以前 version 2 の時に HAL が保証していた条件と同じ）。
  - `vfs.c`: 照合は `scheme == LBA` で start block を比べる。log は共通の version でも scheme が LBA なら今までの「partition LBA=」の行（文は同じ）。LBA の scheme（0）を出すのは PC-98 の native loader だけ（amd64 の HAL は MBR・GPT だけを通す、sun4u は SUN、x68k は X68K、pcat・rpi4・riscv64 は MBR）。
- boot.h から `KERN_HANDOFF_VERSION_PC98`・`KERN_PC98_*`・`struct kern_pc98_parameter_handoff` を外した。

### Raspberry Pi 4（fdt・sdhci は 2026-10-18 Q1 了承）

- Pi 4 には zedBSD の boot loader が無い（firmware が vmunix を直に起こす）。`struct rpi4_boot_handoff` は HAL → kernel の構造体。HAL は `kern_boot_handoff` だけを `kernel_entry` に渡し、固有の情報は次の名前で渡す（HAL が持つ、`src/hal/arm64/bsp-rpi4/boot.c`）。

| 名前 | 中身 | 無い時 |
| --- | --- | --- |
| `rpi4.fdt` | device tree の物理番地（`uint64_t`） | firmware が渡さない時 NULL |
| `rpi4.sdhci` | device tree が名指す SD 制御器の物理番地（`uint64_t`）。`sdhci_irq` は kernel が読んでいないので渡さない | 名指されない時 NULL（kernel は起動の device を出さない、今と同じ） |
| `rpi4.framebuffer.*` | 下の framebuffer の節 | |

### framebuffer（2026-10-18 ユーザーの答え、Q1 経由）

- ユーザーの決定（Q1 の伝え）: 画面の情報は構造体でなく値ごとの名前で渡す（例 `rpi4.framebuffer.phys`・`.size`・`.width`・`.height`・`.pitch`・`.format`、それぞれ整数）。型を HAL と kernel で共有しない。amd64 の `"pcat.framebuffer"`（boot loader の `struct zbl6_framebuffer` を kernel に渡している）も同じ形に直し、WS261 に含める（ユーザー「直す、WS261 に含める」）。
- 形と「値が無い時」の返し方は実装の時にここへ書く。
