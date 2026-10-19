<!-- awesome-plan project=zedbsd record=ws272 -->

# WS272 MIPS64（mips64el）の QEMU malta への移植（framebuffer の console・VFS の段まで）

- ID: ws272、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: planning（2026-10-19 14:13 Q1 作成）
- 起点（2026-10-19 ユーザー）: 「MIP32とMIPS64のカーネルも作りたいです。ただ、ボードがないなあ。virtで作れそうですか？Anbernic RG 350と、Cobalt Qube 初代、EdgeRouter X はあります。MIPS Creatorは購入できそう。」→（クリック）始め方「malta の little endian」、toolchain「許可する」（LLVM に Mips の target、Q1 が行う）、最初の実機「RG350」
- 前提: QEMU の `qemu-system-mips64el -M malta（MIPS64r2 の CPU、little endian）`。画面は malta の PCI の VGA（Bochs の linear framebuffer）を HAL が設定して console を描く案（p001 で確かめる）。共有の LLVM に Mips の target が要る（Q1、ユーザーの許可 2026-10-19）。WS263〜WS266（riscv32・ppc32・ppc64・m68k）と同じ到達点と規則。
- 実機の候補（2026-10-19 14:14 ユーザー）: 「ちなみにQubeはPCI USBボードが使える状態です。DisplayLinkでX11が動いた実績はあります。今回は画面はスコープ外でいいですが。」 → Cobalt Qube（初代、QED RM5230、MIPS IV、little endian、Q1 の覚えで未確認）は PCI の USB の板が使える。Qube の画面（DisplayLink）は範囲外。Qube の実機の WS は着手の時に立てる。
- 浮動小数点（2026-10-19 14:15 ユーザー）: 「Octeonも入手可能です。softfloatをオプションで選べるようにしたいですね。まずはhardfloatでOKです。」 → まず hardfloat（FPU を使う ABI）。softfloat は後で build の option として選べる形にする（p001 の設計で option の置き場所を決めておく）。
- 実機の候補（2026-10-19 14:15 ユーザー）: Cavium Octeon も入手できる（MIPS64、big endian が多い。機種と endian は未確認）。
- 担当: 未割当（WS256 の後か、手の空いた時）。

## 完了の条件（案、p001 の設計で確定）

1. QEMU の上で kernel が起動し、framebuffer（画面）に console が出る。
2. kernel が HAL の初期化を終え、VFS の段まで進み、root が無いので mount できずに止まる。判定は boot-test.sh の画面（serial の log で判定しない）。
3. HAL の API の追加・変更（`include/hal/arch/*.h` など）は、差分ごとにユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。HAL → kernel は汎用の構造体と名前ごとの値だけ。
4. 新しい code は plan/coding-style.md の全文に合う。外部の code の参照は WS245 と同じ規則。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: malta の memory map・PCI（GT-64120）・PIIX4・VGA、firmware（YAMON 相当の -kernel の直の起動）と handoff、mips64el の toolchain の triple と linker の emulation、HAL の構成、HAL の API の差分、試験の道具（boot-test.sh の mode）、Phase の表 | planning |
| p002 | HAL の芯と vmunix、T1 の画面で到達点 | planning |
| p003 | 規約の全文の見直し | planning |

## 依存

- LLVM の Mips の target（Q1、[WS273](../ws273/ws.md) と共通）。
