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
| `rpi4.framebuffer.phys`・`.size`・`.width`・`.height`・`.pitch`・`.format` | firmware の framebuffer（各 `uint64_t`。pitch は byte、format は firmware の pixel order: 0 青が先、1 赤が先） | firmware が割り当てない時（size 0）6 つとも NULL |

### framebuffer（2026-10-18 ユーザーの決定、Q1 経由）

- ユーザーの決定（Q1 の伝え）: 画面の情報は構造体でなく値ごとの名前で渡す（例 `rpi4.framebuffer.phys`・`.size`・`.width`・`.height`・`.pitch`・`.format`、それぞれ整数）。型を HAL と kernel で共有しない。amd64 の `"pcat.framebuffer"`（boot loader の `struct zbl6_framebuffer` を kernel に渡していた）も同じ形に直し、WS261 に含める（ユーザー「直す、WS261 に含める」）。続けて「bootloader/のヘッダを見ていいのは、HALだけにします。HALからカーネルへは、名前ごとの値でアクセスします」「bootloader/にヘッダがあってハンドオフ構造体を定義する分には、OKにします。」（PC-98 の header は HAL の中に作った後なので、そのまま置く、Q1 了承）。
- 形（Q1 了承）:
  - 名前は `<platform>.framebuffer.phys`・`.size`・`.width`・`.height`・`.pitch`・`.format` の 6 つ。値は全部 `uint64_t`。画面が無い時は 6 つとも NULL（一部だけ有ることは無い）。
  - `pcat.framebuffer.*`（`src/hal/amd64/bsp-pcat/boot.c`）: phys・size（mapping の窓の byte）・width・height（pixel）・pitch（pixel、loader の stride）・format（`KERN_BOOT_FRAMEBUFFER_RGBX8888`=1 か `_BGRX8888`=2。loader の `ZBL6_FRAMEBUFFER_*` と同じ値であることを HAL の typedef の検査で保つ）。
  - `rpi4.framebuffer.*`: 上の表。format は firmware の pixel order のままで、汎用の code に揃えていない（揃えるには bcm2711 の driver の読み方も変わる。範囲外、残件）。
  - 構造体を struct ごと返していた名前 `"pcat.framebuffer"` は HAL から外した。HAL の中（`page.c`・`space.c`・`bsp-pcat/cons.c`）は HAL 内部の `bsp_boot_framebuffer()`（`src/hal/amd64/bsp.h`）で loader の `struct zbl6_framebuffer` を直に読む。
  - kernel の中だけの汎用の構造体 `struct kern_boot_framebuffer`（physical_base・size・width・height・pitch・format）と読む関数 `kern_boot_framebuffer(prefix, out)` を `include/kern/platform.h`・`src/kern/pmem.c` に置いた（Q1 了承）。6 つの名前を引いて埋める。無ければ ENOENT、引数の誤り・長すぎる prefix・32 bit を超える値は EINVAL、どちらも record は 0 に戻す。型は kernel だけが持ち HAL とは共有しない。
  - 汎用の画素の形の code `KERN_BOOT_FRAMEBUFFER_RGBX8888`・`_BGRX8888` を `include/kern/boot.h` に足した（`KERN_PARTITION_SCHEME_*` と同じく汎用の値、HAL が loader の値と同じことを検査する）。
- 読む側の追従: `src/drivers/platform/pcat/graphics/backend.c`、`src/drivers/gpu/amdrdna/readout.c`、`src/drivers/gpu/nvrtx/discover.c`・`vram.c`・`nvrtx-private.h`（comment）、`src/drivers/gpu/i915/display/bootfb.c`・`bootfb.h`・`display/xd-display.c`（最小の行だけ）、`src/drivers/gpu/i915/tests/display/host-bootfb-test.c`・`host-kernel.c`（`kern_boot_framebuffer` の stand-in を足した）、`src/kern/platform/rpi4.c`。
- `src/drivers/gpu/i915/tests/render/executor.c`・同 `README.md`（2026-10-18 ユーザーの決定、Q1 経由）: include は無く、文の中で loader の定数の名前 `AMD64_KERNEL_MAX_BYTES` を挙げていただけ。「loader が受ける image の 16 MiB」と書き換えた（試験の意味は同じ）。

## 結果（2026-10-18）

- commit: fa7c63037（PC-98）、4146469e5（RPi の fdt・sdhci）、e6fd1a266（main b5ff6c625 の取り込み、衝突なし）、続く commit（framebuffer の名前、RPi の構造体の削除、pcat の framebuffer、i915 の試験の文）。
- `include/kern/boot.h` に PC-98・RPi の固有の構造体・定数が無い: `git grep -nE 'VERSION_PC98|KERN_PC98_(HANDOFF|PARAMETER)|pc98_parameter_handoff|rpi4_boot_handoff|RPI4_HANDOFF' -- src include bootloader platform` は HAL の中の `struct pc98_parameter_handoff`（`src/hal/i386/bsp-pc98/`）の 2 行だけ。
- kernel・driver が bootloader/ の header を include しない: `git grep -n 'bootloader/' -- src/kern src/drivers include` は 0 件、`git grep -n 'zbl6\|ZBL6\|amd64-handoff\|amd64-kernel-image' -- src/kern src/drivers include` も 0 件。
- build（worktree の中、`make ZEDBSD_CONFIG=config/ci/config-<x>.mk BUILD=build/ws261-<x> <target>`）、全部の変更の後:
  - amd64: `build/ws261-amd64/vmunix` rc=0、warning 0、「amd64 vmunix check: PASS」。
  - rpi4: `build/ws261-rpi4/vmunix` rc=0、warning 0、「arm64 vmunix check: PASS」。
  - pc98: `build/ws261-pc98/vmunix`・`bootloader/BOOTZBSD.EXE` rc=0、warning 0。
  - pcat（i386）: `build/ws261-pcat/vmunix` rc=0、warning 0、「PC/AT vmunix contract: OK」。
- host 試験:
  - `bootzbsd.o` と `payload32.o` の `.text` が基点（cd6392422）の source から作った物と byte で一致（`llvm-objcopy -O binary -j .text` と `cmp`）。
  - `sh plan/ws229/tests/run-bootfb-host-test.sh build/ws261-host/bootfb-test`: 「bootfb_host_test: 20 checks, 0 failures」。
  - `plan/ws261/tests/host-boot-framebuffer-test.c`（`kern_boot_framebuffer` の 10 の確かめ、`src/kern/pmem.c` と host の cc で `-ffunction-sections`・`--gc-sections`）: 「0 failures」。
- 既存の不具合（今回の変更と無関係）: `payload32.elf` の link が `ld.lld: error: discarding .shstrtab section is not allowed` で失敗する。基点の source でも同じ。
- 事故（2026-10-18）: 並列の build の shell の書き方の誤りで、config-pcat.mk の build が main の checkout で走り `build/ws261-pcat`・`.log` を作った。Q1 が消した。toolchain は lock のまま（Q1 が確かめた）。
- 未実施: QEMU の起動の確かめ（Pi 4 の raspi4b、PC-98、amd64 の画面）。T1 への依頼の文面を Q1 に送った。

## 残件

- rpi4 の `format` は firmware の pixel order のまま（汎用の `KERN_BOOT_FRAMEBUFFER_*` に揃えるなら bcm2711 の driver も直す）。
- 範囲外のまま: sun4u・x68k の「XXX: To be removed」の物。
