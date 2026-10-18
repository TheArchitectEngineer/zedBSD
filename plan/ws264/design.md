<!-- awesome-plan project=zedbsd record=ws264-p001-design -->

# WS264 の設計: ppc32 の mac99（framebuffer の console・VFS の段まで）

2026-10-19 P1（q1257、[ws264-p001](phase001/phase.md)）。親: [WS264](ws.md)。machine はユーザーの決定どおり mac99。

## 1. 起動の道（QEMU の source と gdbstub で確認）
- `hw/ppc/mac_newworld.c`: `-kernel` の ELF は `(addr & 0x0fffffff) + 0x01000000` に置かれ、ELF でも a.out でもない file は flat image として `0x01000000` に置かれる。OpenBIOS は fw_cfg の kernel の address（0x01000000）へ入る。
- gdbstub で確かめた（`b .` だけの flat image）: OpenBIOS は image の先頭 0x01000000 へ、OpenBIOS の translation が on（MSR の IR・DR、FP）のまま、r5 = client interface の entry で入る。画面は OpenBIOS の display（`-g 800x600x32` で 32 bpp）。
- よって kernel は **0x01000000 に link した flat image**（`vmunix.bin`、ELF は debugger 用に残す）。
- 起動の段: (1) `_start` が bss を消し stack を取り C へ。(2) OpenBIOS の client interface に `/memory` の reg、`/cpus` の最初の CPU の `timebase-frequency`、display（`screen` alias か `/chosen` の stdout）の address・width・height・linebytes・depth、`/chosen` の bootargs を聞く。(3) translation を切り、BAT・segment register・hashed table を入れ、kernel の MSR で translation を戻す（以後 firmware は使わない）。(4) vector の stub を 0x100〜0x2f00 に写す。

## 2. HAL の構成
- `src/hal/ppc32/`（CPU）: `entry.S`（`_start`、firmware の call、vector の stub（r1・r3・CR を SPRG1〜3 に、user からなら SPRG0 の kernel stack、`ba` で共通の入口へ）、共通の入口（frame を作り translation を on にして C へ、rfi で戻る、MSR の POW を落とす）、task の切り替え（r14〜r31・LR・CR）、新しい task の入口、FPU の保存、MMU の load、idle（HID0 の DOZE と MSR の POW））、`int.c`（vector ごと: decrementer → tick、external → board、DSI/ISI → まず hashed table の再読み込み、sc → system call（r0 番号、r3〜r8 引数）、他は fault）、`space.c`、`task.c`・`page.c`・`lib.c`・`debug.c`・`cpu.c`（m68k virt の物から）、`atomic.c`（UP の enter）。
- `src/hal/ppc32/bsp-mac99/`: `firmware.c`（Open Firmware の client interface、kernel の handoff）、`board.c`（`ppc32_cmain`、bring-up の要約、reset/poweroff は止まるだけ、cache の API）、`clock.c`（decrementer で 100 Hz、time base の counter）、`irq.c`（割り込み controller（OpenPIC）はまだ扱わない: source を登録できない）、`framebuffer.c`（firmware の display に直に描く、32/16/15/8 bpp）、`cons.c`・`font.c`。kernel の platform の binding は arch に依らない `src/kern/platform/riscv-virt.c` を流用（p003 で名前を見直す）。
- address の配置: supervisor は **BAT** で RAM（0〜256 MiB と 256〜512 MiB、coherent）と framebuffer の 16 MiB（uncached・guarded）を物理の address のまま。user は segment 4〜7（0x40000000〜0x7fffffff）を hashed table（64 KiB）で。hashed table は cache: space ごとに 2 段の自前の table を持ち、DSI/ISI で入れ直す。page は書かれるまで read-only で入れ、最初の書き込みの protection fault で modified を記録して writable にする（accessed・dirty を自前の table で正確に持つ）。space ごとに VSID（space id << 4 | segment）、kernel だけの時は何も map しない VSID。
- 制限: UP。外部割り込み（OpenPIC）無し。reset/poweroff は止まるだけ。kernel の W^X 無し（BAT）。

## 3. toolchain
- 共有の LLVM（変更なし）。`--target=powerpc-unknown-elf -D__ZEDBSD__=1 -mcpu=7400 -msoft-float -mno-altivec`、`ld.lld -m elf32ppc`、`llvm-objcopy -O binary`。64 bit の helper は `src/libc/int64.c`。

## 4. HAL の API の差分（事前の承認の範囲）
1. `include/hal/arch.h` に `HAL_ARCH_PPC32` の節。
2. `include/hal/arch/ppc32.h` 新規（riscv32.h と同じ節: tick 100 Hz、signal frame の head 16 byte、atomic は enter（`hal_atomic_uint_try_acquire` は lwarx/stwcx.）、`hal_gpregs`（r0〜r31・pc・msr・lr・ctr・cr・xer）、`hal_fpregs`（f0〜f31・fpscr）、debug point なし、checks）。
3. `hal_get_arch_handoff`: `boot.command-line`。
hal.h・`include/kern/boot.h` は不変。

## 5. HAL の外の変更
- `include/uapi/stat.h`: riscv32 の枝の条件に `KERN_USER_ABI_PPC32` を足した（ppc32 の ABI も 64 bit を 8 byte に揃える）。`include/kern/elf.h` に `EM_PPC`（20）、`src/kern/elf.c` に `HAL_ARCH_PPC32` の枝（ELF32、big endian）。
- `Makefile`・`tools/menuconfig.py` の `ppc32-mac99`、`config/ci/config-ppc32-mac99.mk`、`platform/ppc32-mac99/`。

## 6. 試験
- `plan/tools/boot-test.sh` に `BOOT_MODE=ppc32-mac99`（`-g 800x600x32`）。判定（T1）: 画面に `PPC32 HAL READY DISPLAY FIRMWARE PAGING PASS TIMER TICK`、`PPC32 CPU 1 ONLINE`、`VFS initialization failed`。

## 7. Phase
| Phase | 目的 |
| --- | --- |
| p001 | この設計 |
| p002 | HAL・mac99 の BSP・vmunix・boot-test の mode、T1 の画面で到達点 |
| p003 | 規約の全文の見直し（Q1 の指示、到達点の後） |
