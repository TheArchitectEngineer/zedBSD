<!-- awesome-plan project=zedbsd record=ws266-p002 -->

# ws266-p002 68040 の HAL と virt の BSP（QEMU の直の起動）

Status: cleared（2026-10-19 Q1 判定: T1-713 で画面に M68040 HAL READY DISPLAY VIRTIO-GPU PAGING PASS TIMER TICK・M68040 CPU 1 ONLINE・VFS initialization failed（QEMU、c7af9297a、merge 9358969bf））（旧: in-progress（2026-10-19 P1、q1257。T1 の判定待ち））

- 親: [WS266](../ws.md)。設計: [design.md](../design.md)。
- HAL の API の差分（事前の承認の範囲、朝にユーザーへ報告）:
  - `include/hal/arch.h` に `HAL_ARCH_M68K && HAL_ARCH_M68040` の節（4 行）。適用後 SHA-256 `e7c4888eb83351b605774d0c919b7b3f7e34b61509f0ead03ed25c15f53453d2`（riscv32 の節も含む）。
  - `include/hal/arch/m68040.h` 新規、SHA-256 `fb5699fb585e009e2a84aec4b350ce735594af834f0d2ee219f706758335de21`。
  - `hal_get_arch_handoff`: `boot.command-line`・`m68k.bootinfo`・`m68k.initrd`（`src/hal/m68k/bsp-virt/bootinfo.c`）。hal.h・`include/kern/boot.h` は不変。

## 作った物
- `src/hal/m68k/m68040/`: `entry.S`・`defs.h`・`asm.h`・`int.c/h`・`space.c/h`・`task.c/h`（riscv32 から）・`page.c/h`（同）・`cpu.c`・`lib.c`（同）・`debug.c`（同）・`percpu.h`・`spin.h`。
- `src/hal/m68k/bsp-virt/`: `start.S`・`board.c`・`bootinfo.c`・`irq.c`・`clock.c`・`framebuffer.c/h`（virtio-gpu）・`cons.c/h`・`font.c/h`（riscv の bsp-virt から）・`bsp.h`。
- `platform/m68k-virt/`（`Makefile`・`vmunix.mk`・`vmunix.ld`・`run.mk`）、`Makefile`・`tools/menuconfig.py` の `m68k-virt`、`config/ci/config-m68k-virt.mk`、`plan/ws266/tests/config-m68k-virt.mk`、`plan/tools/boot-test.sh` の `BOOT_MODE=m68k-virt`。

## 確かめた事（host、2026-10-19）
- `make ZEDBSD_CONFIG=config/ci/config-m68k-virt.mk -j16 build/m68k-virt/vmunix`: exit 0、error・warning 0（log `build/tmp/m68k-b2.log`、worktree p1）。ELF32 m68k、entry 0x10000、undefined 0。
- x68k: 変更の前後で error の種類の集合が同じ（`build/tmp/x68k-base.log` と `x68k-after.log` の diff 0、x68k は前から build できない）。
- 解析の短い QEMU（QMP の screendump と gdbstub、判定ではない）: 20 秒後に `m68040_cpu_idle` に居て level 6（tick）の割り込みが続く。画面に `M68040 HAL READY DISPLAY VIRTIO-GPU PAGING PASS TIMER TICK`・`M68040 CPU 1 ONLINE`・`VFS initialization failed (12); entering idle.` を見た。
- 未実施: T1 の boot-test.sh での判定、user の task（user の space・system call・signal）は到達点の外で未試験。

## 残り
- T1 の結果の判定（Q1）。p003 の規約の見直し。
