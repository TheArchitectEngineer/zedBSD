<!-- awesome-plan project=zedbsd record=ws264-p002 -->

# ws264-p002 HAL と mac99 の BSP（OpenBIOS からの起動）

Status: cleared（2026-10-19 Q1 判定: T1-714 で画面に PPC32 HAL READY DISPLAY FIRMWARE PAGING PASS TIMER TICK・PPC32 CPU 1 ONLINE・VFS initialization failed、exit 0（QEMU mac99、694a74202））（旧: in-progress（2026-10-19 P1、q1257。T1 の判定待ち））

- 親: [WS264](../ws.md)。設計: [design.md](../design.md)。
- HAL の API の差分（事前の承認の範囲、朝にユーザーへ報告）:
  - `include/hal/arch.h` に `HAL_ARCH_PPC32` の節（4 行）。適用後 SHA-256 `7649f2942194453f78a1cc6e509b6a7a92c93037630c538db74cc422c7a621db`。
  - `include/hal/arch/ppc32.h` 新規、SHA-256 `343b1e990897d9ccdefccbdde098521a40f2feb16832751034d507976443a77f`。
  - `hal_get_arch_handoff`: `boot.command-line`（`src/hal/ppc32/bsp-mac99/firmware.c`）。hal.h・`include/kern/boot.h` は不変。

## 作った物
- `src/hal/ppc32/`（`entry.S`・`defs.h`・`asm.h`・`int.c/h`・`space.c/h`・`task.c/h`・`page.c/h`・`cpu.c`・`lib.c`・`debug.c`・`atomic.c`・`percpu.h`・`spin.h`）、`src/hal/ppc32/bsp-mac99/`（`board.c`・`firmware.c`・`clock.c`・`irq.c`・`framebuffer.c/h`・`cons.c/h`・`font.c/h`・`bsp.h`）。
- `platform/ppc32-mac99/`、`Makefile`・`tools/menuconfig.py` の `ppc32-mac99`、`config/ci/config-ppc32-mac99.mk`、`plan/ws264/tests/config-ppc32-mac99.mk`、`plan/tools/boot-test.sh` の `BOOT_MODE=ppc32-mac99`。
- HAL の外: `include/uapi/stat.h` の条件、`include/kern/elf.h` の `EM_PPC`、`src/kern/elf.c` の枝（Q1 の確認を依頼）。

## 確かめた事（host、2026-10-19）
- `make ZEDBSD_CONFIG=config/ci/config-ppc32-mac99.mk -j16 build/ppc32-mac99/vmunix.bin`: exit 0、error・warning 0（log `build/tmp/ppc32-b1.log`、worktree p1）。ELF32 PowerPC、entry 0x01000000、undefined 0、`__kernel_end` 0x016b7430（32 MiB の制限の内）。
- 解析の短い QEMU（QMP の screendump と gdbstub、判定ではない）: 20 秒後に `ppc32_cpu_idle`（MSR に POW、`sched_idle` から）。画面に `PPC32 HAL READY DISPLAY FIRMWARE PAGING PASS TIMER TICK`・`PPC32 CPU 1 ONLINE`・`VFS initialization failed (12); entering idle.`。
- 未実施: T1 の boot-test.sh での判定。user の task（hashed table の reload・system call・signal）は到達点の外で未試験。

## 残り
- T1 の結果の判定（Q1）。p003 の規約の見直し。
