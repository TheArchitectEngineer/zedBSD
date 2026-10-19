<!-- awesome-plan project=zedbsd record=ws271-p001 -->

# ws271-p001 設計: MIPS32（mipsel）・MIPS64（mips64el）の QEMU malta

- 親: [WS271](../ws.md)（WS272 の共通の部分も含む）、Queue: q1273 / q1273-i01（P1、2026-10-19）
- Status: in-progress → 設計の文書を書き終えた（2026-10-19 14:22 P1）。clearance は Q1 の判定（ユーザーの判断 U1〜U5 と design-reviewer の要否）。
- 成果: [design.md](../design.md)、[proposed/llvm-mips.md](../proposed/llvm-mips.md)（toolchain の案、Q1 が適用）、[proposed/hal-mips.md](../proposed/hal-mips.md)（HAL の API の差分の案、未適用・承認待ち）、[proposed/boot-test-mips.md](../proposed/boot-test-mips.md)（boot-test の mode の案、Q1 が適用）。code は書いていない。

## 範囲と受け入れ条件
1. QEMU malta の事実（memory map、GT-64120、PIIX4、UART、割り込み、timer、VGA、`-kernel` の受け渡し）を QEMU の source で確かめ、確かめられない事を「未確認」と書く → design.md §0・§1。
2. toolchain（Mips の target、triple、`__ZEDBSD__`、linker の emulation、o32 の hardfloat、softfloat の置き場所） → §2、proposed/llvm-mips.md。
3. HAL の構成（KSEG0/1、TLB の refill、例外、cache）、HAL の API の差分の案、kernel の binding、boot-test の mode、試験の config、Phase の表と LW、ユーザーに聞く判断 → §3〜§11。
4. WS272 は差分だけで済む形 → §8。

## 実行したこと
- `git merge main`（worktree p1、HEAD は 49e5476e1 のまま、新しい merge は無し）。
- QEMU の source を `build/tmp/ws271-docs/` に取得（URL・SHA-256 は design.md §0）。host の QEMU は 10.0.11、`hw/mips/malta.c` は v10.0.3 と v10.0.11 で同一（`cmp`）。
- `build/llvm/bin/clang --print-targets`: Mips が無い（PowerPC・AArch64・RISCV・X86 だけ）。`toolchain/llvm/llvm.mk` 361 行の targets で確かめた。
- `gdb-multiarch` が host に有り、`set architecture mips:isa32r2` を受ける。
- QEMU は起動していない（AGENTS.md の試験の方針）。MIPS の公開の資料（PRA 等）は取得していない。

## 未確認（p002 で gdbstub・monitor で確かめる）
- `-kernel` の entry の CP0 Status（ERL・BEV）と Config.K0。
- PCI の memory 窓 0・1 の終わりの値（`info mtree`）、VGA の slot（`info pci`）。
- std VGA の MMIO の offset の数値（vga-pci の header の定義）、属性 controller の `0x20` の要否。
- `-serial` の 1 番目が Super I/O の COM1 である事。
- QEMU の malta の既定の RAM の大きさ、FPGA の reset の register。
- o32 の 64 bit の member の 8 byte の整列（stat.h の枝の根拠）。
- MIPS64: 64 bit の bootloader の a0〜a3 の符号拡張、`cpu_mips_kseg0_to_phys` の 64 bit の CKSEG0 の扱い。
- vga-pci.c・cpu.c・cp0_timer.c・vl.c・i8259.c の SHA-256 は未記録（file は build/tmp/ws271-docs/ にある）。

## ユーザーに聞く判断
design.md §10 の U1〜U5（HAL の承認の範囲、toolchain の待ち方、FPXX、MIPS64 の CPU、画面の大きさ）。

## 残件・再開の条件
- p002 は LLVM の Mips（Q1）か U2 の (b)、boot-test の mode（Q1）、U1 の承認の後。
- 注記: 作業の中で P1 が `build/tmp/ws271-docs/` の自分の一時 file（`m11.c`、malta.c の v10.0.11 の写しの比較用）を `rm -f` で消した。subagent は rm をしない規則（2026-10-06）に反した。他の file には触れていない。
