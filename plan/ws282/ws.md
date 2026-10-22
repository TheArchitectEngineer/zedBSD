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
| [p001](phase001/phase.md) | 設計（/dev/cam0 の API、RP1 の CSI-2 と CFE、sensor の I2C、firmware の device tree、Linux の rp1-cfe・ov5647・imx219 との照合）→ [design.md](design.md) | — | in-progress（2026-10-23 P3、版 1。Q1 の review と判定待ち） |
| p002 | 枠組み: `include/uapi/camera.h`、`src/drivers/camera/cam.c`（cdev、所有、format、buffer と mmap、queue、poll、control、counters、状態機械）、host の試験（design §11、5〜7 LW） | p001 | planned |
| p003 | RP1 の DesignWare I2C（D2）、`rpicam.c`（dtb、電源の GPIO、両 connector の sensor の検出）、`ov5647.c` の検出、IMX219 の ID の読み。板で chip ID（3〜4 LW） | p002 | planned |
| p004 | `rp1-cfe.c`（clock、DPHY、CSI2 channel、割り込み）、OV5647 の mode と stream、`camcap`。板で最初の RAW の frame（6〜8 LW） | p003 | planned |
| p005 | control・metadata・poll・堅牢化・FAULT の診断・cache の計測（3〜4 LW） | p004 | planned |
| p006 | `CAM_MEMORY_IMPORT`（GPU が確保した buffer を camera が取り込む、D3）、`camvk`（3〜4 LW） | p004、WS279 の V3D | planned |
| p007 | IMX219（D5、module がある時だけ、2〜3 LW） | p004 | planning |
| p008 | `docs/reference/camera.md`、devfs の 0600 と sessiond の seat（Q1 に依頼）、`camcap` を Tools に（1〜2 LW） | p005 | planned |
| p009 | 規約の全文の見直しと license の類似の監査（2 LW） | p006 | planned |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P3: p001 の設計の版 1（[design.md](design.md)）。ユーザーの判断 D1〜D7（design §6: connector、I2C の code の置き方、GPU との共有の向き、node の権限、IMX219 の時期、既定の mode と露光、RAW10 の既定の形）。再開点: Q1 の review の結果と D1〜D7 の答えを受けて p002 から。
