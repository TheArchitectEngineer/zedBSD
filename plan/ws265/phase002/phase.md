<!-- awesome-plan project=zedbsd record=ws265-p002 -->

# ws265-p002 HAL と pseries の BSP（SLOF からの起動）

Status: in-progress（2026-10-19 P1、q1257。T1 の判定待ち）

- 親: [WS265](../ws.md)。設計: [design.md](../design.md)。
- HAL の API の差分（事前の承認の範囲、朝にユーザーへ報告）:
  - `include/hal/arch.h` に `HAL_ARCH_PPC64` の節（4 行）。適用後 SHA-256 `871453538706cd1195d7f92ddd95cdafd5db13c130c2ff38d02d07b1e005a146`。
  - `include/hal/arch/ppc64.h` 新規、SHA-256 `b1db93f5f4ee21681e10060b9b71081b1d6c547e7af1ea370bdc8f3a5abab368`。
  - `hal_get_arch_handoff`: `boot.command-line`。hal.h・`include/kern/boot.h` は不変。

## 作った物
- `src/hal/ppc64/`（`entry.S`・`defs.h`・`asm.h`・`int.c/h`・`space.c/h`・`task.c/h`・`page.c/h`・`cpu.c`・`lib.c`・`debug.c`・`atomic.c`・`percpu.h`・`spin.h`）、`src/hal/ppc64/bsp-pseries/`（`board.c`・`firmware.c`・`clock.c`・`irq.c`・`framebuffer.c/h`・`cons.c/h`・`font.c/h`・`bsp.h`）。
- `platform/ppc64-pseries/`、`Makefile`・`tools/menuconfig.py` の `ppc64-pseries`、`config/ci/config-ppc64-pseries.mk`、`plan/ws265/tests/config-ppc64-pseries.mk`、`plan/tools/boot-test.sh` の `BOOT_MODE=ppc64-pseries`。
- HAL の外: `include/kern/elf.h` の `EM_PPC64`、`src/kern/elf.c` の枝。

## 解析で直した事（gdbstub、serial は使っていない）
1. lld が ELFv2 の `ba`（R_PPC64_ADDR24）を扱わない → stub は CTR で跳ぶ。
2. 16 MiB の page を TCG の pseries が SLB で拒んだ → 64 KiB の page。
3. 新しい task の最初の関数（ELFv2 の global entry）が r12 から TOC を作るので、task の切り替えで r12 に戻りの address を入れる（無いと TOC が 0 で戻り先が 0 になった）。
4. vmspace が 64 bit の user の範囲を 4 GiB より上に求める（`user address range too small`）→ user を 4〜8 GiB に。
5. `page.c` の上限の計算が max_paddr = 全 bit 1 で桁あふれし、全部の確保が NOMEM（`buffer cache initialization failed`）→ 1 を足さない形に（32 bit の port の写しでは max_paddr が 32 bit で起きない）。
6. SLOF の VGA の node に address が無い → PCI の窓から求める。

## 確かめた事（host、2026-10-19）
- `make ZEDBSD_CONFIG=config/ci/config-ppc64-pseries.mk -j16 build/ppc64-pseries/vmunix`: exit 0、error・warning 0（log `build/tmp/ppc64-b1.log`、worktree p1）。ELF64 big endian、entry 0x400000、undefined 0。
- 解析の短い QEMU（QMP の screendump と gdbstub、判定ではない）: 45 秒後に H_CEDE の中（`hal_cpu_idle` ← `sched_idle`）。画面に `PPC64 HAL READY DISPLAY FIRMWARE PAGING PASS TIMER TICK`・`PPC64 CPU 1 ONLINE`・`VFS initialization failed (12); entering idle.`。
- 未実施: T1 の判定。user の task は到達点の外で未試験。

## 残り
- T1 の結果の判定（Q1）。p003 の規約の見直し。
