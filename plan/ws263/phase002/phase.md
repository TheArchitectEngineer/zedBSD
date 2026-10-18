<!-- awesome-plan project=zedbsd record=ws263-p002 -->

# ws263-p002 HAL の芯と vmunix（QEMU virt、OpenSBI からの直の起動）

Status: cleared（2026-10-19 Q1 判定: T1-711 で A（4 CPU）・B（cpus=1）とも画面で HAL READY・SMP N ONLINE・VFS initialization failed を確かめた（QEMU、a194caab9、merge 52ad09aed））（旧: in-progress（2026-10-19 P1、q1257。T1 の判定待ち））

- 親: [WS263](../ws.md)。設計: [design.md](../design.md)。
- HAL の API の差分（事前の承認の範囲、Guardrail の 2026-10-18 の行。朝にユーザーへ報告）:
  - `include/hal/arch.h` に `HAL_ARCH_RISCV32` の 4 行。適用後の SHA-256 `a6f6dee9c81eee19d9d0e454cf986fe98de1f8ba16d598523dd63dd4a980adeb`。
  - `include/hal/arch/riscv32.h` 新規、SHA-256 `2f9616e6bf9c41ab45a213f881fe7801da79fcd47ace26577e0bf21e28f9ee9f`。
  - `hal_get_arch_handoff`: `riscv32.fdt`・`riscv32.initrd`・`riscv32.boot-hart`（`src/hal/riscv32/bootinfo.c`）。
  - hal.h・`include/kern/boot.h` は不変。

## 作った物
- `src/hal/riscv32/`（riscv64 の HAL から: Sv32 の `space.c`、`locore.S`（boot の table を megapage で、jump 無し）、`trap.S`・`dispatch.S`（lw/sw、frame 144 byte）、`asm.h`（CSR を long、`rdtimeh`）、`sbi.c`（set_timer の 2 半分）、`atomic.c`（enter の guard）、`lib.c`（`%l`=long、`%ll`=64 bit）、`page.c`（RAM を direct map の 1 GiB に切る）ほか、`bsp-virt/` は ramfb の console）。
- `platform/riscv32/`（`Makefile`・`toolchain.mk`・`vmunix.mk`・`vmunix.ld`・`run.mk`）。
- `Makefile`・`tools/menuconfig.py` の `riscv32-virt`、`config/ci/config-riscv32-virt.mk`、`plan/ws263/tests/config-riscv32-virt.mk`。
- `src/kern/elf.c` の枝、`include/uapi/stat.h` の riscv32 の assert（Q1 の確認待ち）。
- `plan/tools/boot-test.sh` の `BOOT_MODE=riscv32-virt`。

## 確かめた事（host、2026-10-19）
- `make ZEDBSD_CONFIG=config/ci/config-riscv32-virt.mk -j16 build/riscv32-virt/vmunix`: exit 0、`error:`・`warning:` 0 行（log `build/tmp/rv32-b1.log`、worktree p1）。ELF32、entry 0x80400000。
- `git diff --check` 0。
- 解析の短い QEMU（gdbstub・QMP、判定ではない）: 20 秒後に 4 hart とも `sched_idle`（cpu0 は `kernel_main` から、cpu1〜3 は `kernel_secondary_entry` から）。QMP の screendump で `RISCV32 HAL READY SBI v2.0 PAGING PASS TIMER TICK`・`RISCV32 SMP 4 ONLINE`・`VFS initialization failed (12); entering idle.` を見た。
- 未実施: T1 の boot-test.sh での判定、riscv64・amd64 の build（riscv32 の枝しか変えていない）。

## 残り
- T1 の結果の判定（Q1）。
- riscv64 と riscv32 の HAL の共通化（今回は写し）。
