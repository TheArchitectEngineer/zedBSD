<!-- awesome-plan project=zedbsd record=ws269-p010 -->

# ws269-p010 graphics（TA・3D・transfer・render pass・WSI）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §6 の p010、追補 [design-p010.md](../design-p010.md) 版 1
- Queue: q1362 / q1362-i01（P1、2026-10-20、設計の追補だけ。コードなし）
- Status: planning（2026-10-20 P1: 追補 版 1 を書いた。**design-reviewer のレビュー待ち**。実装は p010a〜p010m の各 Phase で、Queue の承認の後）
- 起点（2026-10-20 07:24 ユーザー）: 「PowerVRのコードは完了を目指して作業をお願いします。あとでUATにおいて、どこまで実行できたかわかるログが出れば、デバッグしやすいと思います。」

## 分割

p010 を 13 の小さい Phase（p010a〜p010m、各 4〜8 LW、合計 80 LW 前後）に分けた。範囲・依存・確かめは [design-p010.md](../design-p010.md) §10。ws.md の Phase の表に写した。

## 決めた事（追補の要約）

- image は LINEAR（rowPitch 32 byte かつ 2 pixel の倍数、WS277 R6 (1)）と twiddled（OPTIMAL）。transfer・render の LINEAR への書き出しは PBE の LINEAR（R6 (2)）。
- render は session ごとの render context・固定 16 MiB の free list（grow・SPM なし、D-P1）・HWRT data set の cache。render pass の instance 1 つ = geometry と fragment の kick の対。
- transfer は 3D の道（TLA の有無は p010b で確かめる）。
- compiler（q1363、P2）との境: compiler は USC の code・temps・shared register の要求の表（SET_BASE・DESCRIPTOR・PUSH_CONSTANTS・LITERAL・WORKGROUP）・varying の表を出し、PDS の program は driver が組む（§5）。
- WSI は readback の道（p010k）を先に、shared の道（p010l）を後に。
- UAT の段の行 `powervr: <段>: ok|fail(<理由>)`（firmware・render node・compute・ta・3d・transfer・present）と sysctl `hw.gpu.powervr.stages`、userland の `gpu-selftest`（段ごとに 1 行、fork と上限時間）を p010a で先に作る（§8）。

## ユーザーの判断（推奨を採って進める、採否はユーザー）

design-p010.md §9 の D-P1〜D-P5（free list と SPM、gpu-selftest の置き方、blend の場所、submit の待ち、段の行の言葉）。

## 確かめ

| 確かめ | 結果 |
| --- | --- |
| 追補の文書 | 書いた。M1・M2 の Mesa の file の path は 26.2.5 の tree で確かめていない（p010b で固定、追補 §0 に未確認と書いた）。L1・L2 の libvulkan・vkdemo の行は 2026-10-20 に読んで確かめた |
| design-reviewer | 未実施（Q1 が依頼する） |

## 残り

- design-reviewer のレビューと対応。
- compiler の境（§5.3）を P2（q1363）と合意する（Q1 経由）。
