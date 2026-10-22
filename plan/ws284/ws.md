<!-- awesome-plan project=zedbsd record=ws284 -->

# WS284 YOLOX-Nano の推論（C と Vulkan compute）

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: planning
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（Pi 5 の CSI の camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件（案）

- YOLOX-Nano（Apache-2.0、ユーザーが選択）を C と Vulkan compute で実装する library（conv・depthwise・BN の畳み込み・SiLU・upsample・concat・head の decode・NMS）。重みは変換して配る（置き場所は設計で決める、license の表示を付ける）。
- 完了: host（Linux の Vulkan）と Pi 5 で、基準の画像の検出の結果が参照の実装と一致する。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（model の構造、重みの形式と変換、層の shader、精度 fp32/fp16、参照の結果の作り方） | — | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
