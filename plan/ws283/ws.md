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
| p001 | 調べと設計（今の compiler・executor の欠け、Mesa v3dv の compute、見積もり） | — | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
