<!-- awesome-plan project=zedbsd record=ws157-p017 -->

# ws157-p017: libjpeg-compat の縮小 decode（DCT scaling）

Parent: [WS157](../ws.md)
Status: in-progress — test-wait（実装・host 試験・build 済み（2026-10-13 P2）。T1 の QEMU の確かめを p015 と 1 本で依頼、Q1 経由）
Phase disposition: normal
Queue: q949（P2、2026-10-13。承認: ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」と 2026-10-11 の L1 の決定「Q1の推しにします」）

## 由来

[p015](../phase015/phase.md) の L1: Photos・Phone の写真の decode を keiland-preview の jail へ移したところ、大きな写真（スマホの 48・50 MP）が preview の上限を越えた。tree の `userland/base/libjpeg-compat`（`decompress.c`）は `scale_num`・`scale_denom` を見ず、`jpeg_start_decompress` で常に全体を decode する（P1 が試して確かめた）。ユーザーの決定: 縮小 decode は別 Phase、それまで preview の上限を Photos の値（64 M pixel・辺 16384・96 MiB）に上げる（p015 で済み）。

## 範囲

- libjpeg-compat に libjpeg と同じ縮小（`scale_denom` 1・2・4・8）、`output_width`・`output_height` は切り上げ。
- `userland/desktop/picture/picture.c` に `kl_picture_jpeg_reduced`（箱の大きさから縮小の比を選ぶ）と keiland-preview の JPEG の decode からの使用。
- 上限の判定を縮小の後の大きさで。
- 試験: libjpeg-compat の縮小の host 試験（libjpeg の出力との比較）、preview の sizes の試験に 200 MP の写真の thumbnail。

## 実装（2026-10-13 P2）

| 所 | 変更 |
| --- | --- |
| `libjpeg-compat/decompress.c` | `jpeg_calc_output_dimensions` が `scale_num`/`scale_denom` から 1/8・1/4・1/2・1/1 を選ぶ（libjpeg 6b と同じ: 求めた比以上で最小の物）。`min_DCT_scaled_size`、出力の大きさ（切り上げ）、各 component の `DCT_scaled_size`（subsample の chroma は upsampling が整数比のままの間 2 倍ずつ大きく、libjpeg-turbo と同じ）と縮小後の `downsampled_width/height`（新しい `decompress_scale`）。plane は縮小の block の大きさで持つ（samples の memory は 1/d²、progressive の係数は全体のまま）。upsampling の比は `decompress_component_ratios`（最大の係数×縮小 / 自分の係数×自分の block）、fancy upsampling は 1/8 で使わない（libjpeg と同じ）。memory の上限の勘定を samples＋係数の byte に |
| `libjpeg-compat/idct.c` | `jpeg_compat_idct_reduced`（4×4・2×2・1×1）: 各 sample は全体の IDCT の下の 2×2・4×4・8×8 の sample の平均。重みは平均した cos の表（13 bit の固定小数点、式を注記、libjpeg の source は写していない）。使う係数の組（2×2 は 0・1・3・5・7）は libjpeg の reduced IDCT と一致 |
| `libjpeg-compat/huffman.c`・`internal.h` | plane に `block_size`・`blocks_across`・`blocks_down`。block の位置・範囲の判定・係数の index をこれで（`jpeg_compat_transform` が全体か縮小の IDCT を選ぶ）。単独 component の scan の block の数は `width_in_blocks`・`height_in_blocks` |
| `desktop/picture/picture.c`・`.h` | `kl_picture_jpeg_reduced(file, data, size, box_side, cover, max_side, max_pixels, …)`: 比 d は cover なら d×箱の長辺 ≤ 画像の短辺、contain なら d×箱の長辺 ≤ 画像の長辺を満たす最大の 8・4・2（どちら向きの EXIF でも足りる）。`kl_picture_jpeg` は box 0（縮小なし）で呼ぶ。**上限の判定を `jpeg_start_decompress` の前**（`jpeg_calc_output_dimensions` の後）に移した（今までは全体を decode してから拒否していた。Image Viewer・Notes・Music も同じ E2BIG を decode の前に返す） |
| `desktop/preview/decode.c`・`preview.h` | JPEG を `kl_picture_jpeg_reduced`（箱は要求の長辺、cover は要求のまま）で。上限（64 M pixel・辺 16384）は縮小後の大きさに掛かる |
| 試験 | 新 `plan/ws157/tests/host-jpeg-scale.c`（driver）と `run-host-jpeg-scale.py`。`run-host-preview-sizes.sh` を改めた（下） |

## 確かめ（2026-10-13 P2、host）

| コマンド | 結果 |
| --- | --- |
| `python3 plan/ws157/tests/run-host-jpeg-scale.py`（新、ASan/UBSan） | all PASS: 16 file（Pillow の 4:4:4・4:2:2・4:2:0・gray・17×9・3×2、cjpeg の 2x1・1x2・2x2・4x1・3x1・4x2・restart・progressive・progressive 4:4:4・q100）× 1/1・1/2・1/4・1/8 で、大きさが djpeg（libjpeg-turbo）の `-scale 1/N` と一致、samples の差は最大 2（1/1 と 1/8 は殆ど 0）、PSNR は 57.7〜99 dB。壊した file 144 回の decode で crash 0 |
| `python3 plan/ws074/tests/run-jpeg-tests.py --keep build/ws157-p017-ws074jpeg`（WS074 の回帰、1/1 の経路） | PASS: 193 file が djpeg と byte まで一致（変更前と同じ）、cut・arithmetic の拒否・markers・damaged 144 で crash 0 |
| `sh plan/ws157/tests/run-host-preview-sizes.sh build/ws157-p017-linux`（preview は `prlimit --as=1 GiB --cpu=10` の下、jail と同じ） | all PASS: 前からの 7 件に加え、72 MP の JPEG の thumbnail（今までは status 3 で拒否）→ 160×160、4096 の箱 → 4096×3641、**200 MP（16320×12240、73 MB）の JPEG** の cover 160・contain 2048・4096 → 160×160・2048×1536・4096×3072、72 MP の PNG（全体を decode）は status 3 で拒否 |
| 200 MP の時間（この host、`/usr/bin/time`） | cover 160: 2.66 s・最大常駐 94 MB、contain 2048: 3.85 s・158 MB、4096: 5.49 s・414 MB（Huffman の decode が大半。73 MB の file の全体を読むため） |
| `sh plan/ws157/tests/run-host-preview-info.sh build/ws157-p017-linux` | all PASS |
| `sh plan/tools/imageview/run-host.sh build/ws157-p017-imageview`（`kl_picture_jpeg` の経路） | PASS |
| `make BUILD=build/ws157-p017 ZEDBSD_CONFIG=config/current-uat.mk -j16 …/bin/keiland-preview …/dynamic/libjpeg-compat.so …/bin/imageview …/bin/files …/bin/notes …/bin/music …/bin/photos …/bin/phone` | rc 0、warning 0 |
| `make BUILD=build/ws157-p017-rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk -j16 …/dynamic/libjpeg-compat.so` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-p017-linux -j16 all` | rc 0、warning 0 |
| `python3 plan/tools/style-check.py`（変えた C と新しい driver） | 指摘は `setjmp` の condition の中（既存の例外と同じ形）だけ。`git diff --check` 0 |

## 未実施・制限

- T1（QEMU、current-uat）: p015 と 1 本で依頼（Q1 経由）。合否: 大きな JPEG（72 MP と 200 MP）を `mediastorage add` した Photos の一覧で thumbnail が出る（「作れなかった」の印でない）、開くと絵が出る。
- FreeBSD の host の build（`Makefile.freebsd`）は未実施（この host に FreeBSD の環境が無い）。
- 実機（Pi 4・Latitude）の時間は未測定。Pi 4 では 200 MP の thumbnail も Huffman の decode が client の 5 秒（`PREVIEW_LIMIT_MS`）を越えうる。越えた写真は「作れなかった」の印になる。
- progressive の JPEG は係数を全体の大きさで持つ（2 byte × 全 sample）ので、200 MP の progressive は約 600 MB で jail の 1 GiB に近い。
- libjpeg-compat は WS074 の design §10 の持ち物（`plan/ws074/design.md`）。縮小への対応の追記は WS074 の記録の側（Q1 の判断）。
- 回帰の試験として `run-host-jpeg-scale.py` を master.md の Tools・試験の一覧に登録するかは Q1 の判断。
