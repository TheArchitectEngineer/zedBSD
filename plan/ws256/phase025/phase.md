<!-- awesome-plan project=zedbsd record=ws256-p025 -->

# ws256-p025 VF2 の skeleton（板の前）

Status: in-progress（code と build は済み、Q1 の判定待ち。2026-10-19 q1278-i01、P2。QEMU・実機の試験は無い: vf2 の vmunix は virt では起動しない）

- 親: [WS256](../ws.md)。Queue: q1278-i01（P2）。
- 範囲: [design-peripherals.md](../design-peripherals.md) §3.10（改訂 4、レビュー 3 Minor 8 を当てた形）。`src/kern/platform/visionfive2.c` の最小、`platform/riscv64/vmunix.mk` の vf2 の枝、config と menuconfig、`src/hal/riscv64/bsp-visionfive2/` の最小の board（`hal_dcache_*` を ccache の FLUSH64 で）。HAL の API（hal.h）は変えない。
- 完了の条件: riscv64 の vf2 と virt の vmunix が warning 0、強い undefined 0（vmunix.mk の検査）、Q1 が合格とする。

## 実装（2026-10-19、P2、code の commit 6933d2fee）

- 名前は design.md §2.2 の案のまま: `ZEDBSD_PLATFORM := visionfive2`、`ZEDBSD_ARCHITECTURE := riscv64`、`ZEDBSD_BOARD := visionfive2`、`-DHAL_BOARD_VISIONFIVE2`、BUILD は `build/visionfive2`（Makefile の riscv64 の BUILD の規則のまま）。
- `Makefile`: `ZEDBSD_PLATFORM_DIR` の riscv64 の filter に `visionfive2`、`ZEDBSD_VARIANTS_riscv64_visionfive2 := default`、`ZEDBSD_PLATFORM_TARGET_visionfive2 := riscv64_visionfive2`。
- `platform/riscv64/vmunix.mk`: `RISCV64_BOARD_DEFINE_visionfive2`・`RISCV64_BSP_DIR_visionfive2`・`RISCV64_BSP_C_visionfive2`（board.c だけ）・`RISCV64_PLATFORM_SOURCES_visionfive2`（visionfive2.c・serial-console.c・ramdisk.c）。PCI の枝は virt だけのまま。
- `tools/menuconfig.py`: PLATFORMS・BOARD_LABELS・BOARD_VARIANTS・MENU_CPUS に `visionfive2`（RISC-V 64 の 2 つ目の board「StarFive VisionFive 2」）。
- `config/ci/config-visionfive2.mk`: `config/ci/config-riscv64-virt.mk` の platform・board の 2 行だけを変えた写し（menuconfig の出力の形のまま）。
- `src/hal/riscv64/bsp-visionfive2/board.c`（新規。ユーザーが作った空の directory、main の checkout では未追跡）:
  - `riscv64_board_init`: DT の `sifive,ccache0`（`riscv64_fdt_info.ccache_base/size`、既存の fdt.c が読む）から FLUSH64 の register を device の窓（`RISCV64_DEVICE_BASE + base + 0x200`）で持つ。ccache が無い・範囲が短い・窓の外なら保守は何もしない。
  - `riscv64_board_putc`: 画面が無いので何もしない（出力は HAL の SBI の console）。`riscv64_board_describe`: `RISCV64 BOARD visionfive2 ccache=<base>` か「unavailable」。
  - `hal_dcache_clean_range`・`_invalidate_range`・`_clean_invalidate_range`: 3 つとも FLUSH64（書き出して捨てる）。kernel の image の窓と direct map の address を物理に戻し、それ以外（device の窓）は飛ばす。`fence iorw, iorw` → line ごとに FLUSH64 に物理 address → `fence iorw, iorw`。手順は OpenBSD `sfcc.c` の `sfcc_cache_wbinv_range`（ISC）を `Procedure referenced:`、値（0x200・64 byte）は OpenBSD `sfcc.c` と Linux v6.12 の `jh7110.dtsi` を `Values referenced:`（Guardrail の出典の規則、block の直前の複数行の comment）。空の stub にしない（非 coherent だった時に黙って壊れないように: レビュー 3 Minor 8）。
- `src/kern/platform/visionfive2.c`（新規）: console は `drv_serial_console_init`（`hal_putc` の teletype）、input は無し（DW UART の RX は p005）、device の表は空、root は loader の initrd（`riscv64.initrd` の handoff、MBR の ramdisk。riscv-virt.c と同じ道）。halt・poweroff・reboot は HAL。`VF2_DMA_DEFAULT_COHERENT` と `drv_fdt_dma_coherent` は p013（design §2.4）。

## build（2026-10-19、BUILD は自分の worktree の build/p025-*）

| 構成 | command | 結果 |
| --- | --- | --- |
| vf2 | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p025-vf2 -j16 vmunix` | exit 0、warning 0（log build/tmp/p025-vf2.log）。`bsp-visionfive2/board.c`・`visionfive2.c` を compile、bsp-virt の file は 0 件。vmunix は ELF の RISC-V、強い undefined 0（vmunix.mk の検査が通った） |
| virt（回帰） | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p025-virt -j16 vmunix` | exit 0、warning 0（build/tmp/p025-virt.log） |

- `hal_dcache_*` は今の vf2 の vmunix では誰も呼ばず（`kern_dcache_*` の使い手が vf2 の構成に無い）、`--gc-sections` で消える。link の要件（design §3.10）は呼ぶ driver が入った時に満たされる。
- clang-format は host に無く未実施。`git diff --check` は 0。

## 見つけた事（範囲の外）

- `plan/tools/menuconfig-target-host-test.py`（Master の Tools の一覧 1289 行、`make menuconfig-host-test`）は **p025 の前から FAIL**: `check_i386_and_retro` が CPU の menu を `['x86_64', 'arm64', 'i386', 'RISC-V 64']` と期待するが、今の menu には `RISC-V 32` もある（riscv32 の追加の後に試験が追従していない）。p025 の変更は CPU の列を変えない（RISC-V 64 の board が 2 つになるだけ）。plan/tools は Q1 の path なので、試験を直すのは Q1 に頼む（期待に `RISC-V 32` を足し、RISC-V 64 の Board の menu に `QEMU virt`・`StarFive VisionFive 2` があることを足す）。

## 残り

- Q1 の判定（merge の後に p013・p005 が着手できる）。
- p005: ccache の mimpid・marchid の log と errata、DW UART の RX、SD の 4 partition の image、実機の login。
