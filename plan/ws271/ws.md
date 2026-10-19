<!-- awesome-plan project=zedbsd record=ws271 -->

# WS271 MIPS32（mipsel）の QEMU malta への移植（framebuffer の console・VFS の段まで）

- ID: ws271、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（p001 を P1 が q1273。2026-10-19 14:13 Q1 作成）
- 起点（2026-10-19 ユーザー）: 「MIP32とMIPS64のカーネルも作りたいです。ただ、ボードがないなあ。virtで作れそうですか？Anbernic RG 350と、Cobalt Qube 初代、EdgeRouter X はあります。MIPS Creatorは購入できそう。」→（クリック）始め方「malta の little endian」、toolchain「許可する」（LLVM に Mips の target、Q1 が行う）、最初の実機「RG350」
- 前提: QEMU の `qemu-system-mipsel -M malta（MIPS32r2 の CPU、little endian）`。画面は malta の PCI の VGA（Bochs の linear framebuffer）を HAL が設定して console を描く案（p001 で確かめる）。共有の LLVM に Mips の target が要る（Q1、ユーザーの許可 2026-10-19）。WS263〜WS266（riscv32・ppc32・ppc64・m68k）と同じ到達点と規則。
- 浮動小数点（2026-10-19 14:15 ユーザー）: 「Octeonも入手可能です。softfloatをオプションで選べるようにしたいですね。まずはhardfloatでOKです。」 → まず hardfloat（FPU を使う ABI）。softfloat は後で build の option として選べる形にする（p001 の設計で option の置き場所を決めておく）。
- 決定（2026-10-19 14:29 ユーザー、クリック）: WS271 の判断（クリック）: U1 HAL の差分は「同じ形で事前承認」（WS263〜266 と同じ形、plan/ws271/proposed/hal-mips.md）、U2 toolchain は「LLVM の Mips を待つ」、U3 MIPS32 の hardfloat の ABI は「FPXX（FR=1）」、U4 MIPS64 の QEMU の CPU は「MIPS64R2-generic」。U5 画面は Q1 が既定の 640x480。
- 担当: 未割当（WS256 の後か、手の空いた時）。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計（[design.md](design.md)、WS272 と共通）: malta の memory map・GT-64120・PIIX4・VGA、`-kernel` の受け渡し、toolchain、HAL の構成、HAL の API の差分の案、試験の道具、Phase の表 | cleared（2026-10-19 Q1） |
| [p002](phase002/phase.md) | 入口と console の画面（共通の層 `src/hal/mips/` の入口、bsp-malta の PCI・VGA・console、build の配線）、LW 12 | cleared（2026-10-19 Q1、T1-725） |
| p003 | 例外・timer・8259・task（user の Status の組み立てを含む）、到達点は `TIMER TICK`（T1）、LW 10 | planning |
| p004 | TLB・space・FPU・atomic と self-test（design.md §6.1）、VFS の段まで（T1 の画面）、LW 12 | planning |
| p005 | 規約の全文の見直し、LW 4 | planning |

設計の改訂 2（2026-10-19 P1、q1277）: design-review-1 の Minor 13 を採り、版 1 の p003（20 LW）を p003・p004 に分け、規約の見直しを p005 にした（[design.md](design.md) §9・§12）。

## 依存

- 2026-10-19 Q1: 共有の LLVM に Mips の target を足した（toolchain/llvm/llvm.mk の targets に Mips、patch level zedbsd10、build/llvm を作り直し rc=0。`clang --print-targets` に mips・mipsel・mips64・mips64el、mipsel の object を `ld.lld -m elf32ltsmip` で link でき ELF32 little endian MIPS、mips64el の compile も可。sysroots も作り直した）。zedBSD の mips の triple（userland 用）はまだ。

- LLVM の Mips の target（Q1、[WS273](../ws273/ws.md) と共通）。
