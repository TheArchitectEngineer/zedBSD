<!-- awesome-plan project=zedbsd record=ws286-p001 -->

# ws286-p001 調べと設計（ARMv7-A の HAL・toolchain・ZYBO の BSP・driver）

- 親: [ws286](../ws.md)、Queue: Q1 の割当（P1、2026-10-23、worktree p1-zybo・branch agent/p1-zybo、基点 main 6206d397e）。
- Status: in-progress（版 2 を書いた。review と U1・U3〜U8 の判断待ち）。
- 成果物: [design.md](../design.md)（版 2）、[proposed/hal-armv7.md](../proposed/hal-armv7.md)、[proposed/boot-test-armv7.md](../proposed/boot-test-armv7.md)。

## 目的と受け入れの条件

- ARMv7-A の HAL（入口・MMU・例外・GIC・timer・SMP・L2・VFP・atomic・TLS・ABI・signal）、toolchain/sysroot、起動の道と license、BSP と driver（UART・SLCR・SD・GEM+RTL8211E・USB・devcfg・reset）、試験、Phase と LW、risk、判断を design.md に書く。
- 受け入れ: Q1 の手配する review で致命の欠落が無く、U1・U3〜U8 の判断が付く（U2 は 2026-10-23 に解決: USB を外す）。
- 版 2 の範囲（2026-10-23 Q1 の割当、P1、worktree p1-zybo2・branch agent/p1-zybo2、基点 main 151b879de）: §12 の決定（OSS だけ、JTAG が先、JTAG で reset、SwitchBot 無し）と、到達目標を「JTAG で起動し PL の block を zedBSD から動かす」に絞る決定（USB を外す）を §4・§7〜§10・Phase の表に入れ、promise した 2 つの proposed を書く。

## 記録（2026-10-23 P1、版 1）

- 読んだ物: design §0 の表（tree の file と、Linux v6.12・U-Boot v2025.07・embeddedsw 2024.2・QEMU v10.0.11 の source。scratchpad に取り、tree には入れていない）。
- 実行した command: `git worktree add …/p1-zybo -b agent/p1-zybo main`、`grep`/`sed`、`qemu-system-arm -M help`・`-cpu help`、`curl` で source の取得。build・QEMU・実機は無し。
- 書けている節: §1（板と SoC の事実、clock、CPU1、QEMU の machine）、§2（toolchain: LLVM に ARM target が無い → Q1 の変更、sysroot の案）、§3（HAL の設計全部、hal.h は不変、arch.h/armv7.h の 3 点は事前承認の形）、§4（SD と U-Boot の起動 — **JTAG 先行の決定の前の版**）、§5（driver）、§6、§7（試験）、§8（Phase、**順の見直しが要る**）、§9（risk）、§10（判断）、§11。
- 未だの物: §12 の決定の反映（JTAG の起動を先に、OpenOCD の script の設計、SwitchBot 無し、OSS だけ）、`proposed/hal-armv7.md`・`proposed/boot-test-armv7.md`（p002 で書く予定だった）、review。
- HAL の API（hal.h）の変更: 不要。arch.h の節と arch/armv7.h の新規、handoff の名前 2 つ（design §3.13、U1）。
- toolchain の変更（Q1 の許可）: `LLVM_TARGETS_TO_BUILD` に `ARM`、patch level 11（p002 の前）。zedbsd の triple `armv7-unknown-zedbsd-eabihf` と sysroot（p008 の前）。
- 計画に無い依存: U-Boot SPL の build（GPL、tree の外、host の cross gcc）。OpenOCD の手順の確認（§12）。

## 記録（2026-10-23 P1、版 2）

- 読んだ物: 版 1 の design・ws・phase、AGENTS.md・Guardrail の HAL の表・Master の Queue の節、`include/hal/arch.h`・`arch/riscv32.h`・`aarch64.h`、`src/hal/riscv32/bootinfo.c`（handoff の名前）・`bsp-virt/`（ramfb の console）、`src/kern/platform/riscv-virt.c`・`visionfive2.c`（ramdisk）・`README.md`、`plan/tools/boot-test.sh`（mode の配線、445 行）・`guest/serial.py`、`include/uapi/syscall.h`、`toolchain/llvm/llvm.mk`・`version.mk`、`plan/ws271/proposed/`（案の形）。外: U-Boot v2025.07（`mach-zynq/spl.c`・`cpu.c`・`u-boot-spl.lds`・`hardware.h`、`common/spl/spl.c`・`spl_ram.c`・`Kconfig`、`arch/arm/Kconfig`、`xilinx_zynq_virt_defconfig`、`board/xilinx/zynq/Makefile`・`board.c`）、QEMU v10.0.11（`hw/arm/virt.c`・`boot.c`）、host の OpenOCD 0.12.0 の `target/zynq_7000.cfg`・`interface/ftdi/digilent-hs1.cfg`・`board/digilent_zedboard.cfg`。外の source は scratchpad に取り、tree に入れていない（SHA-256 は design §0）。
- 実行した command: `git worktree add /home/awe/zedBSD-worktrees/p1-zybo2 -b agent/p1-zybo2 main`、`curl`（source の取得）、`grep`/`sed`/`python3 -I`（文書の編集）、`openocd --version`・`openFPGALoader --help/--list-fpga/--list-boards`・`which`・`dpkg -l`・`build/llvm/bin/clang --print-targets`・`ls /dev/serial/by-id`。build・QEMU・板への操作は無し。
- 分かった事: (1) SPL は JTAG の boot mode で `BOOT_DEVICE_RAM` を選び RAM の中身へ飛び得る → `board_boot_order` の hardware breakpoint で止める、(2) SPL の text base は 0x0・最大 192 KiB、BSS は DDR の 0x100000、`ps7_post_config` は jump の直前だけ → zedBSD が PL の level shifter を開ける、(3) U-Boot 本体の OCM の再配置は SPL では走らない、(4) upstream の OpenOCD の Zynq の cfg に この板の PL の IDCODE 0x03722093 が無い → 自分の cfg、(5) 共有の LLVM に ARM が無い（確認）、(6) QEMU の arm の virt は ramfb を許す（PNG の判定ができる）、zynq には表示が無い。
- HAL の API（hal.h）の変更: 不要。事前承認の形の 3 点（U1、proposed/hal-armv7.md）。
- toolchain の変更（Q1 の許可）: LLVM に `ARM` と armv7 の zedBSD の triple を 1 回の作り直しで（design §10 の依頼 1）。sysroot.mk の armv7（p007 の前）。
- 計画に無い依存: openXC7 の導入（ユーザー、p003 の前）。openXC7 の PS7 の対応は未確認（R14）。
- 未実施: 敵対的 review は design §9 の自分の読み直しだけ（別の担当の review は Q1 の手配）。板の probe は p002。
