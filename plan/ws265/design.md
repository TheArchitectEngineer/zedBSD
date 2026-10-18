<!-- awesome-plan project=zedbsd record=ws265-p001-design -->

# WS265 の設計: ppc64 の pseries、big endian（framebuffer の console・VFS の段まで）

2026-10-19 P1（q1257、[ws265-p001](phase001/phase.md)）。親: [WS265](ws.md)。machine・endian はユーザーの決定どおり pseries・big endian。

## 1. 起動の道（QEMU の source と gdbstub で確認）
- `hw/ppc/spapr.c`: `-kernel` は ELF だけ。segment は `(p_paddr & 0x0fffffff) + 0x400000` に置かれる。SLOF は 0x400000 へ入る。
- gdbstub で確かめた（`b .` だけの ELF、link 0x400000・load address 0）: SLOF は 0x400000 へ、**32 bit mode・real mode**（MSR は FP・ME だけ）、r5 = client interface の entry で入る。
- よって kernel は 0x400000 に link し、load address を 0x400000 引いた所に置く ELF。`_start` が 64 bit mode に入り TOC を設定する。
- 起動の段: (1) bss・stack・C。(2) SLOF の client interface（32 bit mode に戻して呼ぶ）に `/memory@0` の reg、最初の CPU の `timebase-frequency` と `ibm,pft-size`（hashed table の大きさ）、display（`screen` alias か stdout の node）の width・height・linebytes・depth と address を聞く。SLOF の VGA の node は address を持たないので、`assigned-addresses` の一番大きい memory の窓を親の PCI bridge の `ranges` で processor の address に直す。(3) vector の stub を 0x100〜0xf80 に 0x80 ごとに写す。(4) MMU。

## 2. MMU（logical partition）
- hashed table は hypervisor の物: H_ENTER・H_REMOVE で入れる。SLB は自分で（slbia・slbmte・slbie）。
- kernel: RAM（0〜512 MiB）を物理と同じ address に **64 KiB の page**（SLB の L・LP=01、HPTE の L と LP の bit）で入れ、外さない（512 MiB で 8192 個）。framebuffer も同じ。supervisor だけ（PP=00、user の key は 1）。16 MiB の page は TCG の pseries が SLB で受け付けなかった（`Bad page size encoding`）ので 64 KiB にした。
- user: 64 bit の user の layout（kernel の vmspace が brk と mmap を 4 GiB に置く）に合わせて **4 GiB〜8 GiB**（segment 0x10〜0x1f の 16 個、SLB の 3〜18 番）。space ごとに 2 段の自前の table（root 16 KiB、leaf 4 KiB で 2 MiB）を持ち、DSI/ISI で 4 KiB の page を H_ENTER する（hashed table は cache）。書かれるまで read-only で入れ、最初の書き込みの protection fault で modified を記録する（ppc32 と同じ）。
- 例外の stub は割り込まれた側の red zone（ELFv2 の 288 byte）の下に frame を作る。

## 3. HAL の構成
- `src/hal/ppc64/`: `entry.S`（`_start`、firmware の call（32 bit mode）、hypervisor の call、vector の stub（r4・CTR も frame に入れて bctr）、共通の入口、rfid、task の切り替え（r14〜r31・TOC・LR・CR、戻りの address を r12 にも入れて新しい stack の最初の関数が global entry で TOC を作れるようにする）、新しい task の入口、FPU）、`space.c`、`int.c`・`task.c`・`page.c`・`cpu.c`（idle は H_CEDE）・`lib.c`・`debug.c`・`atomic.c`（ppc32 から 64 bit に）。
- `src/hal/ppc64/bsp-pseries/`: `firmware.c`（SLOF の client interface、PCI の窓の変換）、`board.c`、`clock.c`（decrementer、100 Hz）、`irq.c`（XIVE/XICS は扱わない）、`framebuffer.c`・`cons.c`・`font.c`。kernel の platform の binding は `src/kern/platform/riscv-virt.c` を流用。
- 制限: UP。外部割り込み無し。reset/poweroff は止まるだけ（RTAS を使っていない）。

## 4. toolchain
- 共有の LLVM（変更なし）。`--target=powerpc64-unknown-elf -mabi=elfv2 -D__ZEDBSD__=1 -mcpu=power9 -msoft-float -mno-altivec -mno-vsx`、`ld.lld -m elf64ppc`。big endian の ELFv2。

## 5. HAL の API の差分（事前の承認の範囲）
1. `include/hal/arch.h` に `HAL_ARCH_PPC64` の節。
2. `include/hal/arch/ppc64.h` 新規（ppc32.h と同じ節で LP64: signal frame の head 16 byte、`hal_gpregs` は 64 bit）。
3. `hal_get_arch_handoff`: `boot.command-line`。hal.h・`include/kern/boot.h` は不変。

## 6. HAL の外の変更
- `include/kern/elf.h` に `EM_PPC64`（21）、`src/kern/elf.c` に `HAL_ARCH_PPC64` の枝。`Makefile`・`tools/menuconfig.py` の `ppc64-pseries`、`config/ci/config-ppc64-pseries.mk`、`platform/ppc64-pseries/`。

## 7. 試験
- `plan/tools/boot-test.sh` に `BOOT_MODE=ppc64-pseries`。判定（T1）: 画面に `PPC64 HAL READY DISPLAY FIRMWARE PAGING PASS TIMER TICK`、`PPC64 CPU 1 ONLINE`、`VFS initialization failed`。

## 8. Phase
| Phase | 目的 |
| --- | --- |
| p001 | この設計 |
| p002 | HAL・pseries の BSP・vmunix・boot-test の mode、T1 の画面で到達点 |
| p003 | 規約の全文の見直し（Q1 の指示、到達点の後） |
