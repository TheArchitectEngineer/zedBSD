<!-- awesome-plan project=zedbsd record=ws284-p002 -->

# ws284-p002 重み: 変換の script・ZNET・package・参照の生成

- Parent: [ws284](../ws.md)、Queue: q1667 / q1667-i04（P6、実装だけ・試験なし、branch agent/p6-ws282c）
- Status: in-progress（変換の script を流して ZNET の size と SHA-256 を決めた。package・T0 の (b)〜(e)・参照の生成は未実施）

## 書いた物

- `userland/firmware/yolox-nano/yolox-convert.py`（cf0b079e5）: design §2.2 の 1〜5・7。size と SHA-256 の照合を先に、zip の data.pkl を OrderedDict・`_rebuild_tensor_v2`・`torch.*Storage` だけを許す unpickler で読み、連続・storage の範囲を確かめ、§1.3 の graph を topological の順に組み（checkpoint の key: backbone は `backbone.backbone.`、PAFPN は `backbone.`、head はそのまま、Focus の conv は `backbone.backbone.stem.conv`）、BN を float64 で畳み（eps 1e-3）、stem の入力を RGB に並べ替え、§2.3 の ZNET（fp32、`--fp16` は試験用）を書く。構文の確かめだけ（`ast.parse`）。
- `plan/ws284/tests/make-reference.sh`・`make-reference.py`（ffeaeced5）: T1 の venv（torch CPU 版ほか）、YOLOX 6ddff482、.pth と .onnx の取得と SHA-256、画像ごと・416/320 ごとに前処理の済んだ PPM、decode 前の head の出力（f32）、ONNX との差（416）、2 組の閾値の検出。流していない。

## 未実施・残り

- package `yolox-nano-weights`（`zedbsd-firmware.mk` での取得・cache・変換・ZNET の固定の size と SHA-256・install・NOTICE・manifest・README と build の文書の 1 行）。固定の SHA-256 は変換を一度流すまで決められない。
- T0（2 回の出力の一致、読み戻し、ONNX の initializer との照合）、試験の画像 3 枚の選定と license の記録、design の「名前の表は 80 だけ」の意味（op の名前の表と解した、class 名は libyolox の classes.c）。

## 2026-10-29 P6（q1667-i05、agent/p6-ws282d）

- 入力: 前の起動の scratchpad に残っていた `yolox_nano.pth`（7,694,953 byte、SHA-256 `cd28f55f…feff7b` を `sha256sum` で design §0 と一致を確かめた、新たな取得はしていない）。
- 直した事: `yolox-convert.py` が BN の `num_batches_tracked`（LongStorage、重みでない）で「storage LongStorage」と止まっていた。その key は読まずに飛ばす（他の storage の種類は今まで通り拒む）。
- `python3 -I userland/firmware/yolox-nano/yolox-convert.py --input <.pth> --output <scratchpad>/runN.znet` を 2 回: どちらも rc 0、139 op・140 tensor、**3,631,706 byte、SHA-256 `4469dbeb7ad77909690afe17f9ecd4dd6e7995643ac0a34561656b5c4fe73e6d`**（T0 (a) の 2 回の一致。package の Makefile に固定する値）。
- libyolox（host の cc で source から、scratchpad の使い捨ての program）がこの ZNET を load（0）し、CPU の runner（416）で 640x480 の灰（114）の画像を detect: rc 0、検出 0 個、2.40 s（host）。数値の正しさ（T2、PyTorch の参照との照合）は未実施。
- 残り: package `yolox-nano-weights`（上の値を固定）、T0 の (b)〜(e)、参照の生成（make-reference.sh、venv）、試験の画像 3 枚。
