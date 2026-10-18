<!-- awesome-plan project=zedbsd record=ws266-p001-design -->

# WS266 の設計: m68k の virt（framebuffer の console・VFS の段まで）

2026-10-19 P1（q1257、[ws266-p001](phase001/phase.md)）。親: [WS266](ws.md)。

## 1. 68030 で virt が動くか（QEMU の source で確認）
- 読んだ物: QEMU の tree（`~/qemu-pc98`、upstream 由来、host の QEMU 10.0 と同じ系列）の `target/m68k/translate.c`・`helper.c`・`cpu.c`、`hw/m68k/virt.c`。
- 結果: **68030 の MMU は動かない。** `translate.c` が登録する MMU の命令は `pflush`・`ptest` の 68040 の形だけ（`INSN(pflush, f500, …, M68040)`、`INSN(ptest, f548, …, M68040)`）で、68030 の `pmove`（TC・CRP・SRP を読み書きする）は無い。`helper.c` の table walk（`get_physical_address`）も 68040 の 3 段の形（URP/SRP、root・pointer・page）だけ。`cpu.c` の注記も 68030 の PMOVE を未実装と書く。virt の既定の CPU は `m68040`。
- よってユーザーの決定（2026-10-18）どおり **68040 で virt を動かす**。68030 の道（`src/hal/m68k/` の mmu030・cache030・bsp-x68k）は触らない。x68k は handoff の型が無く前から build できず、その壊れの集合は変えない（§6）。

## 2. machine・起動の道・画面
- `qemu-system-m68k -M virt -cpu m68040 -m 512M -device virtio-gpu-device`。QEMU が kernel の ELF を物理の address に置き、MMU 無し・supervisor・割り込み mask 7 で entry へ入る。boot information（tag 付きの record の列: memory・ramdisk・command line・Goldfish PIC/RTC/TTY・virt ctrl・virtio の base）は image（bss を含む）の直後に置かれる。
- 画面: virt に fw_cfg も ramfb も PCI も無いので、**virtio-mmio の virtio-gpu（2D）** を使う。HAL が control queue 1 本を polling で使い、resource（640x400、X8R8G8B8、guest memory を backing）を作って scanout 0 に出し、console の変更ごとに変わった行だけ TRANSFER_TO_HOST_2D と RESOURCE_FLUSH を送る。virtio は little-endian、CPU は big-endian なので byte を入れ替える。
- console・font・80x25 の grid は riscv の bsp-virt と同じ作り。Goldfish TTY にも同じ文字を書く（判定には使わない）。

## 3. HAL の構成
- `src/hal/m68k/m68040/`（CPU の部品）: `entry.S`（全 vector の共通の入口、mask 7 に上げて register を退避し C へ、rte。task の切り替え、新しい task の入口、VBR・URP/SRP/TC/TT・cache・FPU の命令）、`int.c`（vector で分ける: autovector → board、trap #0 → system call（d0 番号、d1〜d5・a0 引数）、format 7 の access fault → kernel の fault handler）、`space.c`（user の table）、`task.c`（riscv32 の task を m68k の frame に）、`page.c`（物理 page の allocator）、`cpu.c`（UP の CPU の API、barrier、MMIO）、`lib.c`・`debug.c`。
- `src/hal/m68k/bsp-virt/`（board）: `start.S`、`board.c`（`virt_cmain`、reset/poweroff は virt ctrl、cache の API）、`bootinfo.c`、`irq.c`（Goldfish PIC 6 個 × 32、PIC n が level n+1、kernel の IRQ API）、`clock.c`（Goldfish RTC の alarm で 100 Hz の tick、ns の counter、epoch 時刻）、`framebuffer.c`（virtio-gpu）、`cons.c`・`font.c`。
- 共有: `src/hal/m68k/atomic.c`（UP、割り込みを止める enter の style）、`src/hal/idle-suspend-unsupported.c`、kernel 側の platform の binding は arch に依らない `src/kern/platform/riscv-virt.c` をそのまま使う。
- address の配置: supervisor は **transparent translation** で RAM（0〜1 GiB、copyback、ITT0/DTT0）と device 空間（0xff000000〜、serialized、DTT1）を物理の address のまま触る。kernel の page table は無い。user の半分は 0x40000000〜0x7fffffff を 3 段の table で（各 table を 1 page に置く）。space の root を URP と SRP の両方に入れるので、kernel は今の space の user の半分を同じ address で触れる。HAL_SPACE_SYS は空の root。
- 制限: kernel の text の W^X は無い（TT は write protect しか持たない）。UP のみ。FPU の内部状態の frame（fsave）は保存しない（task の切り替えは命令の境）。

## 4. toolchain
- 既存の GCC `m68k-linux-gnu-gcc`（14.2）。`-m68040 -msoft-float -ffreestanding`、`-nostdinc -isystem <gcc の include>`、`-D__ZEDBSD__=1 -D_LIBC_LIMITS_H_`（gcc の limits.h が system の limits.h を探さない）。64 bit の除算・bit 数えは `libgcc.a`（`-print-libgcc-file-name`）を link。kernel の source には `-Wno-maybe-uninitialized`（GCC の誤報、devfs.c）。

## 5. HAL の API の差分（事前の承認の範囲）
1. `include/hal/arch.h`: `HAL_ARCH_M68K` かつ `HAL_ARCH_M68040` の節を `HAL_ARCH_M68K` の前に（`<hal/arch/m68040.h>`）。x68k（`HAL_ARCH_M68040` 無し）は従来どおり m68030.h。
2. `include/hal/arch/m68040.h` 新規: m68030.h と同じ signal frame の ABI・tick 100 Hz・enter の atomic に、`hal_gpregs`（d0〜d7、a0〜a7、sr、pc）・`hal_fpregs`（fp0〜7 の 96 bit、fpcr・fpsr・fpiar）・`hal_vregs`・debug point なしの節を足した物。
3. `hal_get_arch_handoff`: `boot.command-line`、`m68k.bootinfo`・`m68k.initrd`（物理の範囲、32 bit の 2 語）。
hal.h・`include/kern/boot.h` は不変。

## 6. HAL の外の変更
- `Makefile`（variant `m68k_virt`、platform target）、`tools/menuconfig.py`（`m68k-virt`）、`config/ci/config-m68k-virt.mk`、`platform/m68k-virt/`。
- x68k の build: 変更の前後で `make … build/x68k/vmunix` の error の種類の集合が同じ（diff 0）。

## 7. 試験
- `plan/tools/boot-test.sh` に `BOOT_MODE=m68k-virt`。判定（T1）: 画面に `M68040 HAL READY DISPLAY VIRTIO-GPU PAGING PASS TIMER TICK`、`M68040 CPU 1 ONLINE`、`VFS initialization failed`。

## 8. Phase
| Phase | 目的 |
| --- | --- |
| p001 | この設計（68030 の確認と 68040 の決定、画面の device） |
| p002 | 68040 の HAL・virt の BSP・vmunix・boot-test の mode、T1 の画面で到達点 |
| p003 | 規約の全文の見直し（Q1 の指示、到達点の後） |
