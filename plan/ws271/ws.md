<!-- awesome-plan project=zedbsd record=ws271 -->

# WS271 MIPS32（mipsel）の QEMU malta への移植（framebuffer の console・VFS の段まで）

- ID: ws271、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: planning（2026-10-19 14:13 Q1 作成）
- 起点（2026-10-19 ユーザー）: 「MIP32とMIPS64のカーネルも作りたいです。ただ、ボードがないなあ。virtで作れそうですか？Anbernic RG 350と、Cobalt Qube 初代、EdgeRouter X はあります。MIPS Creatorは購入できそう。」→（クリック）始め方「malta の little endian」、toolchain「許可する」（LLVM に Mips の target、Q1 が行う）、最初の実機「RG350」
- 前提: QEMU の `qemu-system-mipsel -M malta（MIPS32r2 の CPU、little endian）`。画面は malta の PCI の VGA（Bochs の linear framebuffer）を HAL が設定して console を描く案（p001 で確かめる）。共有の LLVM に Mips の target が要る（Q1、ユーザーの許可 2026-10-19）。WS263〜WS266（riscv32・ppc32・ppc64・m68k）と同じ到達点と規則。
- 担当: 未割当（WS256 の後か、手の空いた時）。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: malta の memory map・PCI（GT-64120）・PIIX4・VGA、firmware（YAMON 相当の -kernel の直の起動）と handoff、mipsel の toolchain の triple と linker の emulation、HAL の構成、HAL の API の差分、試験の道具（boot-test.sh の mode）、Phase の表 | planning |
| p002 | HAL の芯と vmunix、T1 の画面で到達点 | planning |
| p003 | 規約の全文の見直し | planning |

## 依存

- LLVM の Mips の target（Q1、[WS273](../ws273/ws.md) と共通）。
