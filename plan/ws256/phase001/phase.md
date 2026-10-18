<!-- awesome-plan project=zedbsd record=ws256-p001 -->

# ws256-p001 設計: riscv64 の HAL と VisionFive 2 への移植

Status: in-progress（2026-10-18 q1243-i01、P2。design.md の初版を書き、design-reviewer のレビュー中）

- 親: [WS256](../ws.md)。Queue: q1243-i01。
- 範囲: 設計の文書 `plan/ws256/design.md` を書く。code は書かない。HAL の API（include/hal/）の差分は案として plan に置くだけ。
- 完了の条件: design.md に (1) 調べた事実と出典、(2) HAL の riscv64 の構成と API の差分の案、(3) 起動の道（QEMU virt と VisionFive 2）、(4) build の構成、(5) 試験の方法、(6) Phase の表（LW の見積もり）、(7) ユーザーの判断が要る点と既定の案、がそろい、Q1 が読んで合格とする。

## 成果物

- [design.md](../design.md)（§0 出典、§1 事実、§2 HAL の構成、§3 起動の道と loader、§4 build、§5 試験、§6 Phase の表、§7 判断の点、§8 選ばなかった案、§9 失敗・並行性・資源、§10 改訂）。
- [proposed/hal-riscv64.md](../proposed/hal-riscv64.md): HAL の API の差分の案 2 件（`include/hal/arch.h` の 1 行、`include/hal/arch/riscv64.h` の新規）と `include/kern/boot.h` の記録。承認の前は適用しない。

## 実行した事（2026-10-18）

- 読んだ: AGENTS.md、protocol、guardrail、master-design-policy、WS256 の ws.md、既存の HAL（`include/hal/*`、`src/hal/arm64/*`、`src/hal/amd64/` の一覧）、`bootloader/uefi/*`、`bootloader/include/*`、`include/kern/boot.h`、`src/kern/entry.c`・`platform/rpi4.c`・`platform/README.md`、`src/drivers/platform/rpi4/rpi4-console.c`、`platform/arm64/*`、`platform/amd64/vmunix.mk`、`Makefile`、`tools/menuconfig.py`、`toolchain/llvm/*`、`src/rtld/rtld.c`、libc の arch の file、`plan/tools/boot-test.sh`、`plan/ws245/design.md`（書き方の参考）。
- 外部（読むだけ、tree に入れない）: Linux 6.8.12 の RISC-V・StarFive の部分（p1 の tarball から P2 の scratchpad `/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/linux-ext/` に展開）、QEMU v10.0.0 の virt.c/virt.h/boot.c、OpenSBI v1.6、U-Boot v2025.01、riscv-uefi・riscv-sbi-doc・riscv-elf-psabi-doc、StarFive TRM の memory map、Debian の package の一覧。
- host で確かめた（command と結果は design.md §1.8）: 共有 clang の riscv64 の target、`riscv64-unknown-zedbsd` が `__ZEDBSD__` を定義しない事、`riscv64-unknown-windows` が ELF を出し `lld-link /machine:riscv64` が拒む事、`ld.lld -m elf64lriscv -shared` が `R_RISCV_RELATIVE` を作る事。QEMU・実機は起動していない。
- 未実施: QEMU の `-machine dumpdtb`（guest を走らせないが QEMU の起動の規則に触れるので T1/Q1 に残した）、UEFI 2.10 §2.3.7 の再読（uefi.org が 403）。

## commit

- e79c3846d（親 c3722ee0a）: design.md の初版、proposed/hal-riscv64.md。

## 残課題・判断待ち

- design.md §7 の 14 点（既定の案つき）。特に #6（virt に画面が無い: serial.py の対話での判定の例外）、#13（HAL の差分 2 件の承認）、#14（toolchain の patch zedbsd9、Q1）。
- design-reviewer の指摘の反映（design.md §11 に書く）。
