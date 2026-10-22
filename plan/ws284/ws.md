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
| [p001](phase001/phase.md) | 設計（model の構造、重みの形式と変換、層の shader、精度 fp32/fp16、参照の結果の作り方）→ [design.md](design.md) | — | in-progress |
| p002 | 重み: 変換の script・ZNET・参照の生成（PyTorch の venv）・試験の画像・firmware の repository の file・package（4〜6 LW） | p001、D1・D2・D4・D5 | planning |
| p003 | libyolox の骨格と CPU の参照、host の照合 T2（6〜8 LW） | p002 | planning |
| p004 | Vulkan の backend と 7 つの shader、lavapipe の T3〜T5（8〜10 LW） | p003 | planning |
| p005 | zedBSD の package と試験の program `yolox-test`（3〜4 LW） | p004 | planning |
| p006 | Pi 5 の実機の照合と速さの調整（6〜10 LW） | p005、WS283 | planning |
| p007 | 規約の全文の見直しと license の監査（2〜3 LW） | p006 | planning |

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から始める。
- 2026-10-23 P1: p001 の設計の第 1 版（[design.md](design.md)）。Phase の案 p002〜p007（計 29〜41 LW）。再開: design-reviewer とユーザーの判断 D1〜D9（design §7）の後に p002。
