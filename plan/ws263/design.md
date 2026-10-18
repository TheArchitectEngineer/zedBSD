<!-- awesome-plan project=zedbsd record=ws263-p001-design -->

# WS263 の設計: riscv32 の virt（framebuffer の console・VFS の段まで）

2026-10-19 P1（q1257、[ws263-p001](phase001/phase.md)）。親: [WS263](ws.md)。手本は [WS256](../ws256/ws.md) の riscv64 の HAL（p003）。

## 1. machine・firmware・起動の道
- QEMU `qemu-system-riscv32 -M virt -smp 4 -m 512M -device ramfb`。firmware は QEMU の既定の OpenSBI（`opensbi-riscv32-generic-fw_dynamic.bin`、host に有り）。
- kernel は ELF のまま `-kernel` で渡し、OpenSBI が S-mode で入る（a0 = hart id、a1 = DTB の物理）。loader は作らない（WS256 p003 と同じ直の起動）。
- 画面は ramfb（fw_cfg の `etc/ramfb` を DMA で書く）。WS256 の bsp-virt（fwcfg・framebuffer・cons・font）をそのまま使う。console は 80x25、640x400 XRGB8888。

## 2. toolchain
- 共有の LLVM（変更なし）。`riscv32-unknown-zedbsd` は `__ZEDBSD__` を定義しないので、kernel は `--target=riscv32-unknown-elf -D__ZEDBSD__=1`、`-march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -mno-relax`。FP の保存・復元の asm だけ `.option arch, +f, +d`（CPU に D が要る。QEMU の既定の rv32 は D を持つ）。
- 64 bit の除算・shift の helper は `src/libc/int64.c` を LTO 無しで kernel に入れる。userland は作らない。

## 3. HAL の構成
- `src/hal/riscv32/` は riscv64 の HAL の写しを Sv32・32 bit に直した物（共有の file を作ると riscv64 の file を変える必要があるので、今回は写しにした。共通化は後の課題）。
- address の配置（Sv32、root 1024 entry、4 MiB の megapage）:
  - user: `0x00001000`〜`0x7fffffff`（root 0〜511）。
  - direct map: 物理 `0x80000000`〜`0xbfffffff` を同じ address に（root 512〜767）。kernel image は `0x80400000` に link し、そのまま走る（jump も image 窓も無い）。RAM は 1 GiB まで使う。
  - device 窓: 物理 `0`〜`0x3fffffff` を `0xc0000000 + P` に（root 768〜1023）。boot で全部を megapage の RW・非実行で張る（後から root entry を作ると user space の root の写しに届かないため）。
  - image の megapage は page の table に置き換え、text RX・rodata R・残り RW（W^X を boot で確かめる）。
- trap frame 144 byte（32 slot × 4 ＋ sepc・sstatus・scause・stval）、dispatch の 16 word、percpu の `kernel_sp` +4・`user_sp` +8、二次 hart の boot block（satp・stack・cpu・hart、各 4 byte）。
- time: `rdtimeh`/`rdtime` を上が動かなくなるまで読む。SBI の set_timer は 64 bit を a0（下）・a1（上）に分けて渡す。
- atomic: 32 bit の hart は 64 bit の AMO を持たないので `HAL_ATOMIC_STYLE_ENTER`（割り込みを止めて 1 つの guard を amoswap で取る、i386 と同じ形）。
- `hal_printf` の `%l` は long（32 bit）、`%ll` は 64 bit。

## 4. HAL の API の差分（事前の承認の範囲、2026-10-18 Guardrail の行）
1. `include/hal/arch.h` に `HAL_ARCH_RISCV32` の節（`#include <hal/arch/riscv32.h>`）。
2. `include/hal/arch/riscv32.h` を新規（riscv64.h と同じ節: tick 1000 Hz、signal frame の head 16 byte（token・reserved・context_pointer・padding）、atomic は enter の style、`hal_gpregs` は 32 bit の x[32]・pc、debug point なし、checks）。
3. `hal_get_arch_handoff` の名前付きの値 `riscv32.fdt`・`riscv32.initrd`（物理の範囲）・`riscv32.boot-hart`（riscv64 と同じ形）。
hal.h・`include/kern/boot.h` は変えない。

## 5. HAL の外の変更
- `include/uapi/stat.h`: riscv32 の ABI は 64 bit の member を 8 byte に揃えるので `struct timespec` が 16 byte・`struct stat` が 92 byte になる。i386・m68k の ILP32 の assert（12・80）とは別の `KERN_USER_ABI_RISCV32` の枝を足した（userland が無いので ABI は未使用。**Q1 の確認が要る path**）。
- `src/kern/elf.c` に `HAL_ARCH_RISCV32` の枝（ELF32 の EM_RISCV）。
- `Makefile`・`tools/menuconfig.py` に `riscv32-virt`（build は `build/riscv32-virt`）。`config/ci/config-riscv32-virt.mk`（`tests/` に同じ物）。

## 6. 試験の mode
- `plan/tools/boot-test.sh` に `BOOT_MODE=riscv32-virt`（riscv64-virt と同じ形で `qemu-system-riscv32`、既定の kernel `build/riscv32-virt/vmunix`、既定の memory 512M）。既存の mode の振る舞いは不変。
- 判定（T1）: 画面に `RISCV32 HAL READY SBI v… PAGING PASS TIMER TICK`、`RISCV32 SMP 4 ONLINE`、`VFS initialization failed`。

## 7. Phase
| Phase | 目的 |
| --- | --- |
| p001 | この設計 |
| p002 | HAL の芯と vmunix（riscv32 の HAL・platform・build・boot-test の mode）、T1 の画面で到達点を確かめる |
