<!-- awesome-plan project=zedbsd record=ws273 -->

# WS273 Anbernic RG350（Ingenic JZ4770、MIPS32）の実機への移植

- ID: ws273、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19 14:13 Q1 作成。WS271 は完了、設計 p001 を 2026-10-19 に P2 が開始）
- 起点（2026-10-19 ユーザー）: 「MIP32とMIPS64のカーネルも作りたいです。ただ、ボードがないなあ。virtで作れそうですか？Anbernic RG 350と、Cobalt Qube 初代、EdgeRouter X はあります。MIPS Creatorは購入できそう。」→（クリック）始め方「malta の little endian」、toolchain「許可する」（LLVM に Mips の target、Q1 が行う）、最初の実機「RG350」
- 前提（Q1 の覚え、未確認）: JZ4770 は MIPS32r2 の XBurst の 1 core、little endian。内蔵の LCD、SD。起動の道（SD の boot loader）と資料は p001 で確かめる。
- 担当: P2（設計 p001、2026-10-19 q1317）。

## 完了の条件（案、着手の時に確定）

1. RG350 の実機で kernel が起動し、内蔵の LCD に console が出る。
2. 範囲（SD の root、login、入力のボタン）は p001 の設計で決める。

## Phase

| Phase | 中身 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（起動の道・LCD・SD・入力・Phase の分割）。コードは書かない | WS271 | in-progress（設計の文書が出来た、design-reviewer 待ち。2026-10-19 P2、q1317） |

残りの Phase は p001 の設計から作る。

## 依存

- [WS271](../ws271/ws.md)（MIPS32 の HAL の芯）。
