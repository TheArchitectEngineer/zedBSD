<!-- awesome-plan project=zedbsd record=ws284-p002 -->

# ws284-p002 重み: 変換の script・ZNET・package・参照の生成

- Parent: [ws284](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（script だけ書いた、流していない）

## 書いた物

- `userland/firmware/yolox-nano/yolox-convert.py`（cf0b079e5）: design §2.2 の 1〜5・7。size と SHA-256 の照合を先に、zip の data.pkl を OrderedDict・`_rebuild_tensor_v2`・`torch.*Storage` だけを許す unpickler で読み、連続・storage の範囲を確かめ、§1.3 の graph を topological の順に組み（checkpoint の key: backbone は `backbone.backbone.`、PAFPN は `backbone.`、head はそのまま、Focus の conv は `backbone.backbone.stem.conv`）、BN を float64 で畳み（eps 1e-3）、stem の入力を RGB に並べ替え、§2.3 の ZNET（fp32、`--fp16` は試験用）を書く。構文の確かめだけ（`ast.parse`）。
- `plan/ws284/tests/make-reference.sh`・`make-reference.py`（ffeaeced5）: T1 の venv（torch CPU 版ほか）、YOLOX 6ddff482、.pth と .onnx の取得と SHA-256、画像ごと・416/320 ごとに前処理の済んだ PPM、decode 前の head の出力（f32）、ONNX との差（416）、2 組の閾値の検出。流していない。

## 未実施・残り

- package `yolox-nano-weights`（`zedbsd-firmware.mk` での取得・cache・変換・ZNET の固定の size と SHA-256・install・NOTICE・manifest・README と build の文書の 1 行）。固定の SHA-256 は変換を一度流すまで決められない。
- T0（2 回の出力の一致、読み戻し、ONNX の initializer との照合）、試験の画像 3 枚の選定と license の記録、design の「名前の表は 80 だけ」の意味（op の名前の表と解した、class 名は libyolox の classes.c）。
