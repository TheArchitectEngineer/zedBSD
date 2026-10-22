<!-- awesome-plan project=zedbsd record=ws283 -->

# WS283 bcm2711 の Vulkan の compute（Pi 4 V3D 4.2・Pi 5 V3D 7.1）

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: planning
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（Pi 5 の CSI の camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件（案）

- Vulkan の compute の queue・pipeline・dispatch・同期を、kernel の CSD の job につなぐ。compiler が compute の shader（workgroup・invocation の ID、共有 memory、barrier、storage buffer）を 4.2 と 7.1 で通す。
- 完了: Pi 5 の実機で compute の読み戻しの試験が通り、WS284 の層の kernel が動く。Pi 4 は host で確かめ、実機は板がある時。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（今の compiler・executor の欠け、Mesa v3dv の compute、見積もり）（[design.md](design.md)） | — | in-progress（2026-10-23 P2、第 1 版、判定と D1〜D4 待ち） |
| p002（案） | encoder: branch・MULTOP・UMUL24・BARRIERID・TMUWT・TIDX（4.2・7.1） | p001 | 3〜4 LW |
| p003（案） | compiler の直線の compute（payload・ID・整数・storage の TMU・push） | p002 | 8〜12 LW |
| p004（案） | 分岐と loop（execute の mask、uniform の巻き戻し、block の scheduler） | p003 | 8〜12 LW |
| p005（案） | 共有 memory と barrier | p004 | 5〜7 LW |
| p006（案） | 実行器（queue・storage の descriptor・compute pipeline・dispatch → CSD・indirect） | p003 | 10〜14 LW |
| p007（案） | Pi 5 の実機の確認（T1）と直し | p005・p006 | 4〜8 LW |
| p008（案） | 速さ（vector の TMU、ldunifa、fp16 の保存、dispatch の費用） | p007 | 8〜12 LW |
| p009（案） | Pi 4（4.2）の host、実機は板がある時 | p007 | 2〜4 LW |
| p010（案） | 規約の全文の見直しと license の監査 | p002〜p009 | 3〜4 LW |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P2: p001 の設計の第 1 版（[design.md](design.md)）。p002〜p010 の案で 51〜77 LW。再開点: Q1 の判定とユーザーの D1〜D4（design §5）。
