<!-- awesome-plan project=zedbsd record=ws284 -->

# WS284 YOLOX-Nano の推論（C と Vulkan compute）

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: incomplete・保留（設計だけ、実装は後日）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-23 ユーザー「…設計だけ残してラップアップして、後日実装することにします。」（WS282〜WS285））。旧: incomplete（p001 の設計は済み、実装は後日）。残り・制限: p002〜p008
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（Pi 5 の CSI の camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件（案）

- YOLOX-Nano（Apache-2.0、ユーザーが選択）を C と Vulkan compute で実装する library（conv・depthwise・BN の畳み込み・SiLU・upsample・concat・head の decode・NMS）。重みは build が公式の .pth を取得して変換する（2026-10-23 ユーザーの判断、変換した重みはどの repository にも置かない）。LICENSE と NOTICE を重みの横に install する。
- 完了: host（Linux の Vulkan）と Pi 5 で、基準の画像の検出の結果が参照の実装と一致する。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（model の構造、重みの取得と変換、層の shader、精度、参照の結果の作り方）→ [design.md](design.md)（第 2 版） | — | in-progress |
| [p002](phase002/phase.md) | 重み: build の中の変換の script・ZNET・package `yolox-nano-weights`（取得・確かめ・変換・固定の SHA-256）・T0・参照の生成（PyTorch の venv、416・320）・試験の画像（5〜7 LW） | p001 | in-progress（script だけ、未実行、agent/p6-ws282c） |
| [p003](phase003/phase.md) | libyolox の骨格と CPU の参照、ZNET の確かめ、host の照合 T2・T2b（7〜9 LW） | p002 | in-progress（CPU の参照は実装、試験未実施、agent/p6-ws282c） |
| p004 | Vulkan の backend と 15 の SPIR-V、入力の段と固定の graph、分割、debug、lavapipe＋validation の T3〜T6・T9（10〜13 LW） | p003 | planning |
| p005 | zedBSD の package と試験の program `yolox-test`（3〜4 LW） | p004 | planning |
| p006 | Pi 5 の実機の照合と速さの調整（6〜10 LW） | p005、p008、WS283 p004・p006・p007 | planning |
| p007 | 規約の全文の見直しと license の監査、回帰の試験と data を tests/・Tools へ（2〜3 LW） | p006 | planning |
| p008 | V3D 7.1 の host の compile の確かめ（15 の SPIR-V、2〜3 LW） | p004、WS283 p004 | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P1: p001 の設計の第 1 版（[design.md](design.md)）。Phase の案 p002〜p007（計 29〜41 LW）。再開: design-reviewer とユーザーの判断 D1〜D9（design §7）の後に p002。
- 2026-10-23 P1: p001 の第 2 版（review F1〜F22 とユーザーの判断。重みは build の中で変換）。Phase に p008（V3D 7.1 の host の compile）を足し、計 35〜49 LW。再開: Q1 の判定とユーザーの D10（license の yes/no を出すか、推し: 出さない）・D11（COCO の試験の画像を tree に、推し: 入れる）の後に p002。
- 2026-10-23: ユーザーの判断で設計だけ残して止める（「設計だけ残してラップアップして、後日実装することにします。」）。再開は実装の指示から。
- 2026-10-23: ユーザーの決定「D10は出さない、D11は入れる」→ D10 重みの license の yes/no の確認は出さない（LICENSE・NOTICE を重みの横に）、D11 試験の COCO の画像 3 枚を tree に入れる。

## 実装だけの担当 P6（2026-10-29、q1667）

- 2026-10-29 01:3x ユーザー「P6は設計が決まっているWSについて、テストなしでひたすら実装だけ進めてテスト未完了でincompleteだけど実装はできていてマージされていない状態のWSを増やす。」
- ユーザー「調査しなくてよい、設計ができているか自明なWSについて、ひたすら書いてください。選択したもののうち、調査が必要なものはスキップします。WSごとにサブのサブエージェントを作ってよいです。」（選択: WS223・WS282〜WS284・WS243・WS296）
- 解釈（Q1）: 試験をせず実装だけを P6 の branch に積み、main に merge しない。Status は incomplete のまま（試験は未実施）。調べが要ると分かれば P6 は飛ばして Q1 に返す。
- 2026-10-29 P6（q1667-i04、agent/p6-ws282c）: p002 の変換・参照の script、p003 の libyolox（CPU の参照）を実装。試験は未実施。再開点は [phase003](phase003/phase.md)。p004（Vulkan）は未着手。
