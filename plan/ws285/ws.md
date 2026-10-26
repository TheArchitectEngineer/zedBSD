<!-- awesome-plan project=zedbsd record=ws285 -->

# WS285 camera の object detection の demo の app

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: planning・保留（後日）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-23 ユーザー「…設計だけ残してラップアップして、後日実装することにします。」（WS282〜WS285））。旧: planning。残り・制限: p001 設計
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（Pi 5 の CSI の camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件（案）

- `/dev/cam0` の RAW を GPU の compute で debayer・縮小し、WS284 で検出して、bounding box と class を重ねて Keiland の window に表示する。
- 完了: Pi 5 の実機で camera の前の物体に box が出る。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（画面、debayer、pipeline の組み方、fps の目標） | WS282・WS283・WS284 の設計 | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23: ユーザーの判断で設計だけ残して止める（「設計だけ残してラップアップして、後日実装することにします。」）。再開は実装の指示から。
