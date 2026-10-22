<!-- awesome-plan project=zedbsd record=ws283 -->

# WS283 bcm2711 の Vulkan の compute（Pi 4 V3D 4.2・Pi 5 V3D 7.1）

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: planning
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（Pi 5 の CSI の camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件

- Vulkan の compute の queue・pipeline・dispatch・同期を、kernel の CSD の job につなぐ。compiler が compute の shader（workgroup・invocation の ID、共有 memory、barrier、storage buffer）を 7.1 と 4.2 で通す。
- 完了（D1・D2、2026-10-23 ユーザー「decisionは推しでいいです」）: Pi 5 の実機で compute の読み戻しの試験が通り、WS284 の層の kernel が GPU で動いて参照と一致し、速さを測って報告する（fps の数値の目標は置かない、足りなければ 320×320）。Pi 4（4.2）は host の試験で確かめ、実機は板がある時（完了の条件の外）。
- 範囲の外（D4）: atomic・storage image・subgroup。fp16 は保存だけ（D3、p011）。

## Phase

7.1 を先に作り、4.2 は p013 の port（design §1.12）。番号は design 第 2 版の物（第 1 版の p003〜p010 とは対応しない）。

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（[design.md](design.md)） | — | in-progress（2026-10-23 P2、第 2 版、Q1 の判定と Q-a〜Q-c 待ち） |
| p002（案） | encoder: branch（cond・msfign・ub）・MULTOP・UMUL24・BARRIERID・TMUWT・TIDX（4.2・7.1）、HT1 | p001 | 3〜5 LW |
| p003（案） | 試験の道具: lane の model（plan/tools/v3d-qpu/ へ移した物）の 7.1 の版を 16 lane・thread・batch・TMU・barrier・branch に、独立の rule checker | p002 | 6〜9 LW |
| p004（案） | compiler の直線の compute（binary の interface、payload・ID、整数と除算、storage の TMU と clamp、push） | p003 | 8〜12 LW |
| p005（案） | 実行器（CONFIG の gate、queue・限界、storage の descriptor、compute pipeline、dispatch → CSD と分割、indirect） | p004 の interface | 10〜14 LW |
| p006（案） | 最初の Pi 5 の実機の確かめ（T1）と直し: HW0〜HW3・HW5・HW7 | p004・p005 | 4〜8 LW |
| p007（案） | 分岐と loop（msfign=P、divergence、mask、uniform の巻き戻し、MOVE の合併）、WS284 の conv1x1 が 2 thread で compile | p006 | 9〜13 LW |
| p008（案） | block を持つ一般の scheduler（7.1） | p007 | 8〜12 LW |
| p009（案） | 共有 memory と barrier（barrier を含む loop の規則） | p008 | 5〜7 LW |
| p010（案） | 2 回目の Pi 5 の実機の確かめ（T1）と直し: HW4・HW6・HW8〜HW10 | p009 | 4〜8 LW |
| p011（案） | 速さ（vector の TMU、NonWritable と ldunifa、L2T の clean の省略、dispatch の続けての実行、fp16 の保存） | p010 | 8〜12 LW |
| p012（案） | 公開（CONFIG の既定を y、noct の accel・libglesv2・vkreset の確かめ） | p011 | 1〜3 LW |
| p013（案） | 4.2 の port（scheduler の block 化、lane の model、HT の 4.2 の版） | p011 | 8〜14 LW |
| p014（案） | 規約の全文の見直しと license の監査 | p002〜p013 | 3〜4 LW |

計 77〜121 LW（design §4）。WS284 の Pi 5 の Phase は p007 の後に始められる。

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P2: p001 の設計の第 1 版（[design.md](design.md)）。p002〜p010 の案で 51〜77 LW。再開点: Q1 の判定とユーザーの D1〜D4（design §5）。
- 2026-10-23 P2: design-reviewer の review（[phase001/review-20261023.md](phase001/review-20261023.md)、High 6・Medium 10・Low 7）とユーザーの D1〜D4（推しの通り）、Q1 の D5・H1・M5 を入れた第 2 版。p002〜p014 で 77〜121 LW。再開点: Q1 の判定と Q-a〜Q-c（design §5）、D5 の file の移動。
