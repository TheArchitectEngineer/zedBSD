<!-- awesome-plan project=zedbsd record=ws273 -->

# WS273 Anbernic RG350（Ingenic JZ4770、MIPS32）の実機への移植

- ID: ws273、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19 22:22 ユーザー「RG350はきりのいいところでラップアップしましょう。途中までのコードをコミットしてOKです。」で中断。p001・p002（T1-748 待ち）・p003 の途中（bsp の最小・platform・uImage、main 7adce0d6d）まで。実機 1 は beta2.md の U26。再開は phase003/phase.md の手順から）
- 起点（2026-10-19 ユーザー）: 「MIP32とMIPS64のカーネルも作りたいです。ただ、ボードがないなあ。virtで作れそうですか？Anbernic RG 350と、Cobalt Qube 初代、EdgeRouter X はあります。MIPS Creatorは購入できそう。」→（クリック）始め方「malta の little endian」、toolchain「許可する」（LLVM に Mips の target、Q1 が行う）、最初の実機「RG350」
- 前提（Q1 の覚え、未確認）: JZ4770 は MIPS32r2 の XBurst の 1 core、little endian。内蔵の LCD、SD。起動の道（SD の boot loader）と資料は p001 で確かめる。
- 担当: P2（設計 p001、2026-10-19 q1317）。

## 完了の条件（案、着手の時に確定）

1. RG350 の実機で kernel が起動し、内蔵の LCD に console が出る。
2. 範囲（SD の root、login、入力のボタン）は p001 の設計で決める。

## 決定（2026-10-19 21:00 ユーザー、クリック）

機種 RG350（320×240）。D1 A（UZIMAGE.BAK）、D2 A、D3 A、D4 A、D5 B、D6 A、D7 承認（実機の目視・写真を証拠、Guardrail の表）、D8 A（mips32.h の comment の差分を承認、Guardrail の表）。原文は Master の decisions-log。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（起動の道・LCD・SD・入力・Phase の分割）。コードは書かない | WS271 | cleared（2026-10-19 Q1、改訂 2.1。D1〜D8 は未決） |
| [p002](phase002/phase.md) | MIPS の共通の層の変更（Cause・0x200、PageCtrl、入口の cache、early mark、timer の bsp 化、VTAG・L2、console の共通化、試験の切り替え）。malta は不変 | p001 | 実装済み・T1 の結果待ち（2026-10-19 P1 q1327、commit 8b44436a0。build 4 種 warning 0、host 試験 PASS） |
| [p003](phase003/phase.md) | bsp-jz4770 の最小（入口、LED・backlight の印、platform/mips32-rg350、uImage の script、link 0x80600000） | p002 | in-progress（2026-10-19 P1 q1329、ユーザーの指示でラップアップ。build warning 0・host 試験済み、実機 1 は未実施） |

残りの Phase は p001 の設計から作る。

## 依存

- [WS271](../ws271/ws.md)（MIPS32 の HAL の芯）。
