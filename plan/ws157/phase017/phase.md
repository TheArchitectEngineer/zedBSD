<!-- awesome-plan project=zedbsd record=ws157-p017 -->

# ws157-p017: libjpeg-compat の縮小 decode（DCT scaling）

Parent: [WS157](../ws.md)
Status: planned（2026-10-11 ユーザーの決定 L1 で新設、P1 が記録）
Phase disposition: normal
Queue: 未定（Q1 が割り当てる）

## 由来

[p015](../phase015/phase.md) の L1: Photos・Phone の写真の decode を keiland-preview の jail へ移したところ、大きな写真（スマホの 48・50 MP）が preview の上限を越えた。tree の `userland/base/libjpeg-compat`（`decompress.c`）は `scale_num`・`scale_denom` を見ず、`jpeg_start_decompress` で常に全体を decode する（P1 が試して確かめた）。ユーザーの決定: 縮小 decode は別 Phase、それまで preview の上限を Photos の値（64 M pixel・辺 16384・96 MiB）に上げる（p015 で済み）。

## 範囲（案）

- libjpeg-compat に libjpeg と同じ縮小（`scale_denom` 1・2・4・8）: IDCT を 4×4・2×2・1×1 に縮めた物（libjpeg の `jidctred` と同じ考え、写さずに書く）、`output_width`・`output_height` を `jpeg_calc_output_dimensions` と同じく切り上げで。
- `userland/desktop/picture/picture.c` に `kl_picture_jpeg_reduced`（箱の大きさから縮小の比を選ぶ: cover は短い辺、contain は長い辺、どちら向きでも足りる最大の比）と keiland-preview の JPEG の decode からの使用。p015 の作業で一度書いて戻した形（git の 02009ba53 の前の作業の記録は phase015 にある）。
- 上限の判定を縮小の後の大きさで（大きな写真も 1/8 で thumbnail が作れる）。
- 試験: libjpeg-compat の縮小の host 試験（libjpeg の出力と PSNR の比較）、preview の sizes の試験に 200 MP の写真の thumbnail。

## 見積もりと依存

約 3 LW。依存: p015（統合の後）。base の library（libjpeg-compat）の変更なので、その持ち主の WS の確認が要るかは Q1 が判断する。
