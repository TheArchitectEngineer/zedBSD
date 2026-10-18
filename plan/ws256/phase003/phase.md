<!-- awesome-plan project=zedbsd record=ws256-p003 -->

# ws256-p003 HAL の芯（QEMU virt、SBI からの直の起動）

Status: in-progress、test-wait（2026-10-18 20:39、q1249、P2。実装と build（warning 0）と host 試験は済み。T1 の依頼は Q1 に送った（番号は Q1）。T1 の結果を Q1 が判定するまで cleared にしない）

- 親: [WS256](../ws.md)。Queue: q1249（承認: 2026-10-18 ユーザー（クリック）「p009 → p003 を今始める」）。
- 範囲: [design.md](../design.md) §6 の p003。HAL の芯と最小の kernel 側、vmunix の build。loader（p004）・kernel の console と ramdisk（p008）・userland（p007）は範囲の外。
- HAL の差分: 承認済み（[Guardrail](../../guardrail.md) の 2026-10-18 の行、ユーザー（クリック）「3 件とも承認」）。[proposed/hal-riscv64.md](../proposed/hal-riscv64.md) の差分 1・2 を案の文面のまま適用し、差分 3（名前付き handoff）を `src/hal/riscv64/bootinfo.c` に実装した。`include/hal/hal.h` と `include/kern/boot.h` は変えていない（#9 の既定 (a)）。
  - `include/hal/arch.h` SHA-256 `8e21a97f0e1e802ec972ef22f055bba9dc560d3b9bcc2b5bbd594a7ca41f5ec8`（diff の SHA-256 `a83333cb…894a96`）
  - `include/hal/arch/riscv64.h` SHA-256 `09263ce6116fa79a807c901baffb258ef3a93ded0fc2d50addbf374036c867a2`（案の code block の文字どおり）

## 作った物

- `src/hal/riscv64/`: `locore.S`（両入口、boot の table: 低い 256 GiB の識別 map・direct map 128 GiB・device 窓 64 GiB を 1 GiB の leaf で、image 窓を 2 MiB の leaf 8 個で）、`trap.S`（sscratch の約束、user からは percpu の `kernel_sp` へ、`csrw sscratch, zero` を直ちに、kernel からの trap は tp を戻さない）、`dispatch.S`（s0〜s11・ra、新しい task の入口、FP の保存・復元）、`cmain.c`、`bootinfo.c/h`（`boot.command-line`・`riscv64.fdt`・`riscv64.initrd`・`riscv64.boot-hart`、MULTIBOOT/MBR/1/0x80 の handoff、`cpus=`）、`fdt.c/h`、`sbi.c/h`（Base・TIME・IPI・RFENCE・HSM・SRST・DBCN、legacy の putchar）、`percpu.c/h`、`spin.h`、`smp.c`（HSM で 1 つずつ、1 秒の待ち、IPI の理由の word）、`int.c/h`（trap の振り分け、CIP-453 の符号拡張）、`irq.c/h`（PLIC の source、mask = priority 0）、`plic.c/h`、`clock.c/h`（常に SBI set_timer、deadline を書いてから `kernel_timer_handler`）、`page.c/h`、`space.c/h`（Sv39、kernel 半分は init で確定して封じる）、`task.c/h`（user の tp は frame の x4）、`debug.c`、`asm.c/h`、`lib.c`、`defs.h`、`bsp.h`。
- `src/hal/riscv64/bsp-virt/`: `board.c`（fw_cfg → ramfb → console の再描画、dcache は no-op）、`fwcfg.c/h`（signature・DMA の bit・directory・DMA の書き込み）、`framebuffer.c/h`（640x400 XRGB8888、`.bss` の 1 MiB）、`cons.c/h`（80x25、最初の文字から grid を持ち、画面が出た時に全部を描く）、`font.c/h`（PC/AT の 8x16。`src/drivers/platform/pcat/graphics/vgafont.c` と 4096 byte が一致することを確かめた）。
- `platform/riscv64/`: `Makefile`・`toolchain.mk`・`vmunix.mk`・`vmunix.ld`・`run.mk`。
- `src/kern/platform/riscv-virt.c`（boot device 0、debug_write・halt・reboot・poweroff）、`src/kern/elf.c` の `HAL_ARCH_RISCV64` の枝、`include/kern/elf.h` の `EM_RISCV`、`src/kern/platform/README.md` の一覧。
- `Makefile`（`riscv64-virt` → `platform/riscv64`、`BUILD := build/riscv64-virt`、triple `riscv64-unknown-zedbsd`、variant、`-DHAL_ARCH_RISCV64`）、`tools/menuconfig.py`（PLATFORMS・BOARD_LABELS・BOARD_VARIANTS・MENU_CPUS、Desktop を出さない CPU に riscv64）、`config/ci/config-riscv64-virt.mk`、`plan/ws256/tests/config-riscv64-virt.mk`（同じ内容）。

## design との違い（実装で決めた事。HAL の API は不変）

1. `/memreserve/` の範囲は allocator から外すが direct map からは外さない。direct map から外すのは `/reserved-memory` の `no-map` の node だけ（Linux と同じ扱い。`/memreserve/` に DTB 自身が入る場合に、DTB を direct map で読めなくなるのを避けた）。design M4 は両方を外すと書いていた。
2. `hal_irq_register` は enable の bit を立てるだけで priority は変えない（0 のまま＝mask。driver が `hal_irq_unmask` で priority 1）。arm64 の register が mask を変えないのに合わせた。design §2.2 は register で priority 1 と書いていた。
3. image 窓は root の entry 510 の下を 4 KiB の page で張り、text を RX、rodata を R、残りを RW にした（W^X を `RISCV64 W^X PASS` で確かめる）。device 窓は `DEVICE_BASE + 物理` の固定の offset の 2 MiB の leaf（同じ device を何度 map しても同じ entry）。
4. boot の table の識別 map は RAM の範囲だけでなく低い 256 GiB の全体（virt と VF2 の両方の RAM・device を覆う）。
5. `src/kern/ptrace.c` は riscv64 の kernel に入れない（`syscall.c` は x86・aarch64 以外で ptrace を `ENOSYS` にする）。
6. `hal_entropy_fill` は常に false（design は DT の `zkr` を見て `seed` CSR を読む案。U74 も QEMU の既定の rv64 も `zkr` を持たないので後回し）。
7. compiler は `riscv64-unknown-zedbsd`（p010 (a) の zedbsd9 が `__ZEDBSD__` を定義する）で、design §1.8 の `riscv64-unknown-elf`＋define の回り道は要らなくなった。

## 確かめた事（host、2026-10-18）

- `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix`: exit 0、`warning:`・`error:` 0 行（log `build/tmp/rv-build3.log`、worktree p2）、`llvm-nm -u` に強い undefined 0。ELF は RISC-V、entry 0xffffffff80000000、最初の PT_LOAD の paddr 0x80200000、image 約 9.2 MB（16 MiB の assert の内）。locore.o の逆 assemble で boot の table の 4 つの loop と satp の値を目で確かめた。
- `plan/ws256/tests/fdt/run.sh OUTDIR`: 34 件 PASS（virt 風の DTS: hart 2・timebase・`riscv,isa-extensions` と `riscv,isa` の両方・memory・`/memreserve/` と reserved の 3 件・no-map 1 件・PLIC の context 1/3・ns16550a・fw_cfg・bootargs・initrd・短い buffer の拒否。JH7110 風: hart 5 で usable 4（S7 を外す）・timebase 4 MHz・context 2h・DW UART の reg-shift 2・ccache・no fw_cfg）。
- `plan/ws256/tests/plic/run.sh OUTDIR`: 21 件 PASS（plic.c・irq.c の写しを riscv の命令の無い asm.h・spin.h の代わりと並べ、偽の PLIC の register の配列で: init で priority 0・context 1 の enable 0・threshold 0・残った claim の complete、register は enable を立て priority 0 のまま、unmask で priority 1、mask で priority 0 で enable は残る、claim した source は handler の EOI で 1 回 complete、handler の無い source は mask して complete、local の ack は何も complete しない、unregister、範囲外の source、level-high だけ受ける）。
- 静的な検査: trap frame の offset（`int.h`）、percpu の `kernel_sp`・`user_sp`（`percpu.h`）、boot block（`smp.c`）、ramfb の設定 28 byte（`framebuffer.c`）の `_Static_assert`。
- `git diff --check` 0。
- 未実施: QEMU での起動（T1 に依頼）、page table・fw_cfg の byte 列の host の fixture（design §5.4 の一部。space.c は direct map の高い address に table を書くので host で動かせない。page table は T1 の画面の `RISCV64 PAGING PASS`・`W^X PASS` と code review で、fw_cfg は ramfb の画面が出る事で代える）、amd64 の build（Makefile・elf.c の変更は riscv64 の条件の中だけ）。

## T1 への依頼（Q1 経由）と合否

- 走り A（`riscv64-virt`、`-smp 4`、initrd 無し）: 画面に `RISCV64 PAGING PASS`・`RISCV64 SBI v`・`RISCV64 TIMER TICK`・`RISCV64 SMP 4 ONLINE`・`vfs: … failed`（root が無い）。
- 走り B（`BOOT_APPEND=cpus=1`）: `RISCV64 SMP 1 ONLINE`。
- 走り C（`QEMU_EXTRA="-cpu rv64,sstc=off,svpbmt=off"`）: A と同じ行。
- sifive_u（#17、gdbstub）: `cpu_count` が 4、`percpu_records[i]`（112 byte ずつ。`hart_id` +32、`ready` +40、`timer_ticks` +88）の i=0〜3 で ready 1、ticks > 0。`percpu_records[0].hart_id` は記録（≠0 なら hart id ≠ logical id の道を通った）。

## 残り

- T1 の結果の判定（Q1）。FAIL なら gdbstub で止めて `bt` と register を見る（P2）。
- `plan/tools/menuconfig-target-host-test.py`（MAC-T001）の CPU の一覧に `"RISC-V 64"` を足す 1 行（Q1 の path）。
- `tests/virt.dts`（dumpdtb、T1/Q1）を得たら FDT の host 試験の入力に足す。
- p008 で: kernel の console（`ns16550.c`・`serial-console.c`）、ramdisk、arch の `#if` の棚卸し。
