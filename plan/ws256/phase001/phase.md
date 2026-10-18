<!-- awesome-plan project=zedbsd record=ws256-p001 -->

# ws256-p001 設計: riscv64 の HAL と VisionFive 2 への移植

Status: in-progress（2026-10-18 q1243-i02、P2。design.md の改訂 2 を書いた。Q1 の合格で cleared）

- 親: [WS256](../ws.md)。Queue: q1243-i01（初版）、q1243-i02（改訂 2）。
- 範囲: 設計の文書 `plan/ws256/design.md` を書く。code は書かない。HAL の API（include/hal/）の差分は案として plan に置くだけ。
- 完了の条件: design.md に (1) 調べた事実と出典、(2) HAL の riscv64 の構成と API の差分の案、(3) 起動の道（QEMU virt と VisionFive 2）、(4) build の構成、(5) 試験の方法、(6) Phase の表（LW の見積もり）、(7) ユーザーの判断が要る点と既定の案、がそろい、レビュー 1 の全件が反映され、Q1 が読んで合格とする。

## 成果物

- [design.md](../design.md)（§0 出典と license の決定、§1 事実、§2 HAL の構成、§3 起動の道と loader、§4 build、§5 試験（画面の決定）、§6 Phase の表、§7 判断の点（決まった物・残る物）、§8 選ばなかった案、§9 失敗・並行性・資源、§10 改訂、§11 レビュー 1 の反映の表）。
- [proposed/hal-riscv64.md](../proposed/hal-riscv64.md): HAL の API の差分の案 2 件（`include/hal/arch.h` の 1 行、`include/hal/arch/riscv64.h` の新規）と、`hal_get_arch_handoff` の riscv64 の名前付き object（HAL の責務の追加）。承認の前は適用しない。
- [proposed/boot-h-riscv64.md](../proposed/boot-h-riscv64.md): `include/kern/boot.h` の差分の案 (b)。既定は (a)（boot.h を変えない）で、ユーザーの決定の前は適用しない。
- [design-review-1.md](../design-review-1.md): レビュー 1（Q1 が要約して置いた）。

## 実行した事

### q1243-i01（2026-10-18、初版）

- 読んだ: AGENTS.md、protocol、guardrail、master-design-policy、WS256 の ws.md、既存の HAL（`include/hal/*`、`src/hal/arm64/*`、`src/hal/amd64/` の一覧）、`bootloader/uefi/*`、`bootloader/include/*`、`include/kern/boot.h`、`src/kern/entry.c`・`platform/rpi4.c`・`platform/README.md`、`src/drivers/platform/rpi4/rpi4-console.c`、`platform/arm64/*`、`platform/amd64/vmunix.mk`、`Makefile`、`tools/menuconfig.py`、`toolchain/llvm/*`、`src/rtld/rtld.c`、libc の arch の file、`plan/tools/boot-test.sh`、`plan/ws245/design.md`（書き方の参考）。
- 外部（読むだけ、tree に入れない）: Linux 6.8.12 の RISC-V・StarFive の部分（p1 の tarball から P2 の scratchpad `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/linux-ext/` に展開）、QEMU v10.0.0 の virt.c/virt.h/boot.c、OpenSBI v1.6、U-Boot v2025.01、riscv-uefi・riscv-sbi-doc・riscv-elf-psabi-doc、StarFive TRM の memory map、Debian の package の一覧。
- host で確かめた（command と結果は design.md §1.8）: 共有 clang の riscv64 の target、`riscv64-unknown-zedbsd` が `__ZEDBSD__` を定義しない事、`riscv64-unknown-windows` が ELF を出し `lld-link /machine:riscv64` が拒む事、`ld.lld -m elf64lriscv -shared` が `R_RISCV_RELATIVE` を作る事。QEMU・実機は起動していない。
- commit e79c3846d（親 c3722ee0a）。design-reviewer を起動し、結果は Q1 が受け取って design-review-1.md に置いた（blocker 1・major 13・minor 24）。

### q1243-i02（2026-10-18、改訂 2）

- main e4c67abc5 を p2 に ff で取り込んだ。子のエージェントは起動していない（Q1 の指示）。
- レビュー 1 の全件（B1、M1〜M13、minor 1〜24）を反映し、design.md §11 に項目 → 直した節の表を置いた。B1 は (a)（ramdisk の中身を rpi4 と同じ MBR の image、handoff は MULTIBOOT/MBR/1/0x80）、M7 は boot.h の差分を proposed/boot-h-riscv64.md に置き既定は boot.h を変えない名前付き handoff object（ユーザーの判断 #9）、M9 の sysroot.mk は p010 (b)、M10 の TLS は「static TLS 無し・全部 dynamic、`-ftls-model=global-dynamic -mtls-dialect=trad --no-relax`、TPREL64 を checker で拒む」で設計で決めた、M12 は (1)(2) を既定（timer は常に SBI、Svpbmt は使わない、sifive_u の走り）。
- ユーザーの決定 5（virt に画面）: ramfb を選び（virtio-gpu は virtqueue と 2D の command の driver が要る）、framebuffer の console の出所は kernel 側の HAL の bsp-virt（fw_cfg の DMA で `etc/ramfb` に設定を書く。U-Boot の `qemu-riscv64_smode` は ramfb の driver を持たず、GOP は bochs-display を付けた時だけと推定。HAL が作れば直の起動と loader の起動で同じ画面）、640x400 で `boot-test.py` の 80x25 の cell にちょうど合わせる。Phase ごとの判定は design.md §5.2 の表（p003 は HAL の boot log の行、p008 は ramdisk の mount と init の不在、p007/p004 は login の prompt と serial で打った `uname -m` の答えの画面）。実機の判定は未決（#15）。決定 6（WS245 と同じ）を §0.2 に決定として書いた。
- 確かめた事（tree の読み取りと公開の source、design.md §10 に一覧）: vfs.c の root の選択の行、rpi4 の handoff と device の表、boot.h の全文、arm64 space.c の table と SW_DIRTY、elf.c・rtld.c・static-tls.c の TLS、sysroot.mk の arch、uaccess.c、hal.h の contract、platform.h の hook、Makefile・menuconfig の表、boot-test.sh/.py の画面の読み方、elf64.c、Linux の irq-sifive-plic.c の mask（priority 0/1）、QEMU v10.0.0 の virt.c（fw_cfg の DMA、ramfb の許可、memmap）・boot.c（boot_hart 0、DTB・initrd の置き場）・Kconfig・ramfb.c（設定の形）・fw_cfg.rst・cpu.c（`sstc` 既定 true、`svpbmt` 既定 false）、U-Boot v2025.01 の qemu-riscv64_smode の defconfig と board の Kconfig（VIDEO・VIDEO_BOCHS は imply）・visionfive2.rst（SPL は type GUID で探す、boot hart 2 の log）。
- 推測のまま残した物（design.md §10 の一覧）: QEMU rv64 の V の既定、Debian の U-Boot の binary の video の有無、`IMAGE_FILE_MACHINE_RISCV64` の値、U74 の rdtime の emulation と A/D、OpenSBI の IPI の届け方、sifive_u での boot hart、arm64 の `long double` の builtins。
- ws.md の Phase の表を design §6 に合わせた（p009 の中身を「試験の道具」に変更、p010 を 3 つの部分に）。
- QEMU・実機は起動していない。build は走らせていない（code を書いていない）。`-cpu help`・`dumpdtb` も走らせていない（QEMU の起動の規則に触れるので T1/Q1）。

## commit

- e79c3846d（親 c3722ee0a）: design.md の初版、proposed/hal-riscv64.md。
- 改訂 2 の commit の SHA は Q1 への報告に書く（この file の commit の後なのでここには書けない）。

## 残課題・判断待ち

- design.md §7.2 の 16 点（既定の案つき）。p003 の着手の前に要る物: #13（HAL の差分 2 件と名前付き object の承認）、#17（sifive_u の判定）、#18（p009 の tools の担当）。p008 の前: #9（boot.h）。p007 の前: #14（toolchain、Q1）。p005 の前: #15（実機の判定）、#10・#2・#8。
- Q1 の合格（レビュー 2 は Q1 が段取りする）。
