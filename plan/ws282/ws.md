<!-- awesome-plan project=zedbsd record=ws282 -->

# WS282 camera の driver の枠組み（/dev/cam0）と CSI の camera の driver「rpicam」

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG006。Related: MG008（Pi 5）。
- Status: planning
- 出典: 2026-10-23 ユーザー「では、MIPI CSIのカメラから取り込んだ画像をYOLOで画像認識することをゴールにして、・/dev/cam0 ... camera driver frameworkと、それに準拠した CSI camera driver "rpicam"・Pi5 VideoCore VIIでの compute・YOLOシリーズの非常に小さなモデルをC言語とVulkan Computeで実装・カメラ入力をobject detectionしてbounding boxを表示するデモアプリを構築 ここまでやりたいです。」
- 関係: WS282（camera）・WS283（compute）・WS284（YOLOX-Nano）・WS285（demo）の 4 つで 1 つの到達目標（Pi 5 の CSI の camera の画像を YOLOX-Nano で検出し、bounding box を表示する）。

## 目的と完了の条件（案）

- `/dev/cam0`…: camera の driver の枠組み。format は YUV・RGB・RAW を取れる interface とし、今回は RAW だけ実装する（ユーザー「/dev/cam0 はYUV, RGB, RAWで取得できるインターフェースとする。今回はRAWのみ実装。あとでPiSPを通したYUV/RGBも実装。」）。
- `rpicam`: Pi 5 の CSI（RP1 の camera front end、MIPI CSI-2）と sensor（I2C）の driver。sensor はユーザーの手元の 5 MP の module（ユーザー「5MPのものです。v2ですかね。」→ 5 MP は Camera Module v1 の OV5647 の見込み、v2 は 8 MP の IMX219。chip ID を I2C で読んで判定し、実機で確かめる）。
- 完了: Pi 5 の実機で RAW の frame が連続して取れ、userland の試験の program で画像として確かめられる。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（/dev/cam0 の API、RP1 の CSI-2 と CFE、sensor の I2C、firmware の device tree、Linux の rp1-cfe・ov5647・imx219 との照合） | — | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
