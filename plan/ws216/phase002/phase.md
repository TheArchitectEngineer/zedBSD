<!-- awesome-plan project=zedbsd record=ws216-p002 -->

# ws216-p002: libpdf の拡張（係数の輪郭・範囲の輪郭、writer の状態・Flate・粒の pattern、reader の Flate の hash・tiling pattern）

Parent: [WS216](../ws.md)
Status: in-progress（2026-10-13 P1。設計を書いてから実装）
Phase disposition: normal
Queue: Q1 の依頼（2026-10-13、P1）。承認は ws.md の「ユーザーの決定」（D1〜D12 は推奨で決定）と review-2 の判定「p002 は条件つきで始めてよい」

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md)（§5.1〜§5.3、§8 の p002 の行）。review は [review-1](../phase001/review-1.md)・[review-2](../phase001/review-2.md)。
- review-2 §5 の条件: M-6・M-7・M-8・M-9 をこの phase.md の設計と受け入れに入れてから実装する。M-13・L-3 もここで書く。L-16 は試験に入れる。
- 触る file: `userland/base/libpdf/`（`outline.c`・`writer.c`・`writer.h`・`update.c`・`reader.c`・`content.c`・`display.c`・`raster.c`・`internal.h`・新しい `paint.c`・3 つの Makefile・`exports.map`）、`include/libc/pdf.h`、`plan/ws216/`。Notes の source（`userland/desktop/notes/`）はこの段では触らない（p003 以降）。HAL・toolchain に触れない。
- 試験の方針（AGENTS.md 2026-10-03）: build（amd64・rpi4、warning 0）と host の試験（plain・ASan・UBSan）まで。QEMU は無い（libpdf の画面の確かめは p007 で T1）。

## 1. 係数の輪郭（p001 §5.1）

### 1.1 公開の形（`include/libc/pdf.h`）

```c
struct pdf_factor_point { double x; double y; double factor; };        /* 位置（pt）と幅の係数 */
struct pdf_outline_style { double floor; double exponent; double factor_max; double radius_min; };
int  pdf_outline_stroke_style(points, count, width, style, &outline, &outline_count);
struct pdf_outline_range { size_t start; struct pdf_point *corners; size_t count; size_t seam_start; size_t seam_end; };
int  pdf_outline_stroke_ranges(points, count, width, style, cuts, cut_count, &ranges, &range_count);
void pdf_outline_ranges_free(ranges, range_count);
#define PDF_OUTLINE_NO_SEAM ((size_t)-1)
```

- 半径 = `max(radius_min, width × (floor + (1 − floor) × f^exponent) / 2)`。f は点の係数を `0..factor_max` に clamp した値を、今の筆圧と同じ Hermite の補間（両端の値の間に収める）で区間の中へ広げた物。NaN は EINVAL。Notes の筆・鉛筆・水彩は `floor 0・exponent 1・factor_max 2.0・radius_min 0.05`。
- `pdf_outline_stroke()`（ペン・蛍光ペン）は今のまま。内部は同じ関数を style `{0.15, 0.7, 1.0, 0}` で通るが、式の評価の順は今の `pressure_radius()` と同じなので bit 同一（試験 T1）。

### 1.2 範囲の輪郭（鉛筆の段の区間、M-8・M-9）

- **cuts の意味（M-9 (a)）**: cuts は呼び手の点の配列の index の列で、狭義の増加、各 `1 ≤ cut < count`。cut c は「点 c から新しい段が始まる」の候補。libpdf は点 c に当たる中心線の頂点から**弧長の増える向き**（「後ろ」の意味）へ、次の cut の頂点の手前（最後の cut は中心線の最後の頂点の手前）までの間で、最初の「継ぎ目に使える頂点」へ境をずらす。見つからなければその cut を捨てる（その段の変化は捨てられる、L-14）。
- **返す物（M-9 (a)）**: 区間の配列。`start` はその区間の段を決める点の index（最初の区間は 0、他は採った cut の値）。呼び手は `steps[start]` でその区間の段を引く。中心線の頂点の index は外に出さない（呼び手は頂点を知らない）。
- **継ぎ目の辺（M-9 (b)）**: 辺は「最初の角の index」で名指し、角 i の辺は角 i から角 `(i + 1) mod count` へ。`seam_start` はその区間の始めの切り口の辺（区間が中心線の最初から始まるなら `PDF_OUTLINE_NO_SEAM`）、`seam_end` は終わりの切り口の辺（同じく最後なら NO_SEAM）。p004 の画面はこの 2 辺に fringe を出さない。角の順は今の輪郭と同じ（左の縁を前へ、終わりの cap か切り口、右の縁を後ろへ、始めの cap か切り口）なので、`seam_start` はいつも `count − 1`（閉じる辺）、`seam_end` は左の縁の最後の角の index。
- **継ぎ目に使える頂点（M-8）**: 内側の頂点で、gentle（今の `measure_join()` の判定、片側の縁に角 1 つ）、かつ弧長でその頂点の半径の 2 倍以内の前後の頂点が全て gentle で、その範囲の曲がりの絶対値の和が 0.35 rad 以下。切り口は middle の法線の上の平らな線分で、隣の区間はその 2 角を同じ式で計算するので bit で同じ値。
- **自分と重なる stroke（M-8）**: 区間に分けた輪郭は区間ごとに別の fill になるので、区間をまたいで自分と重なる所（ループ、急な曲がりの直後の切り口の近く）は二度塗られる（画面と PDF で同じ、制限として記録）。「重なりも隙間も無い（面積の和 = 全体）」の受け入れは、自分と重ならない stroke に限る。上の条件（前後の曲がりの和）は切り口の近くの重なりを避けるための物で、ループは避けない。
- 動かない stroke（中心線が 1 点）は区間 1 つの点（cuts は捨てる）。

## 2. writer（p001 §5.2・§5.3）

### 2.1 状態（`q`・`Q`、alpha と blend、M1）

- `pdf_writer_save_state()`（`q`）・`pdf_writer_restore_state()`（`Q`）。writer は PDF の状態（今の alpha、今の blend）と求められた blend を 16 段の stack に積み、`Q` で戻す。積みすぎ・空の `Q` は EINVAL。
- `pdf_writer_set_blend_mode(writer, NORMAL | MULTIPLY)`: 次の `pdf_writer_set_fill_color()`・`pdf_writer_set_fill_grain()` から効く（その時に (alpha, blend) が PDF の状態と違えば `gs` を書く。画像も今どおり最後の色の状態で描く）。
- ExtGState の鍵は (ca, BM)。数の上限は今の 64 のまま。`/BM` は Multiply の state に書き、Normal の state には文書が Multiply を一つでも使う時だけ `/BM /Normal` を書く（Multiply を使わない文書の bytes は今と同じ。p003 の「1.1・2.0 の file を今と同じ bytes で書く」）。

### 2.2 content の Flate（D8、L-3）

- `pdf_writer_set_content_flate(writer, 1)`（新しい `paint.c`）で、その writer の頁の content（新しい文書の頁と、update の新しい content の stream）を保存の時に Flate で書く。既定は今どおり無圧縮（Notes は major 3 の時だけ有効にする、p003）。圧縮で短くならない頁と、`PDF_WRITER_FLATE_CONTENT_MAX`（64 MiB）を超える頁は無圧縮で書く。
- hash: writer の `pdf_writer_get_page_content_hash()` は今どおり圧縮前の bytes の SHA-256。reader（2.4）は Flate だけを decode してから hash を取り、decode 後が 64 MiB を超えたら ENOTSUP。writer は 64 MiB を超える頁を圧縮しないので、自分の保存した頁が上限で CHANGED になることは無い（L-3: 上限の定数は `writer.h` の 1 つを reader と共有）。
- writer.c は zlib に依らない（Flate は `paint.c` が writer に付ける関数で行う。embed.c の font と同じ形）。既存の host 試験（`run-pdf-writer.sh` などは writer.c を zlib なしで link する）を壊さない。

### 2.3 粒の tiling pattern（D11、M-6・M-13）

- `pdf_writer_add_grain(writer, coverage, width, height, &grain)`: 8 bit の濃さ（行 0 が上、最大 1024 角、文書に 8 つまで）を文書の 1 つの SMask（DeviceGray、Flate）として持つ。色の間で共有。
- `pdf_writer_set_fill_grain(writer, grain, r, g, b, alpha, cell)`: 以後の fill をその粒の tiling pattern で塗る（`/Pattern cs /<prefix>Pn scn`）。alpha と blend は `set_fill_color()` と同じ扱い。`set_fill_color()` で単色に戻る（`rg` が DeviceRGB に戻す）。
- pattern: PatternType 1・PaintType 1・TilingType 1、BBox `[0 0 1 1]`・XStep 1・YStep 1。cell の content は `q 1 0 0 -1 0 1 cm /G Do Q`（画像の最初の行を cell の上へ）。pattern は**自分の `/Resources`**（`/XObject << /G n 0 R >>`、色の画像）を持つ（M-13: clean copy が pattern の content の名前を自分の resources で追う）。色の画像は width × height の一色の DeviceRGB（Flate）で `/SMask` が粒。(粒, 色) ごとに 1 つ。
- **頁ごとの Matrix（M-6）**: Matrix = (cell 倍の拡大) × (その頁の「見えている頁の座標 → 頁の既定の座標」の行列)。新しい頁は `[1 0 0 -1 0 h]`、他の PDF の上の頁は `shown_to_user()`（crop box の原点と `/Rotate`）。pattern の鍵は (粒, 色, cell, 頁の行列) なので、高さの違う頁・回転した頁はそれぞれの pattern を持ち、粒は見えている頁の左上を原点に cell ごとに繰り返す（画面の式と同じ所に同じ texel）。
- 名前は writer の `name_prefix` を付ける（`<prefix>P<n>`）。update の頁の resources の合わせ（`write_merged_resources`）に Pattern の分類を足し、`choose_prefix()` の調べにも Pattern を足す。
- 文書の上限: pattern 1024、色の画像 256。

### 2.4 reader の content の hash（C1）

- `pdf_document_page_content_hash()`: 無圧縮の stream は今どおり bytes をそのまま。`/Filter /FlateDecode`（名前か 1 要素の配列、`/DecodeParms` なし）の stream は decode してから hash。他の filter・DecodeParms つきは今どおり ENOTSUP。

## 3. reader の tiling pattern（S1、M-7）

- `scn` が PatternType 1・PaintType 1 の pattern を名指したら fill の色にする（PaintType 2 は今どおり飛ばして SKIPPED）。fill は新しい display item `PDF_ITEM_PATTERN`（`pdf.h`）: path と rule は fill と同じ、`pixels`・`image_width`・`image_height` は 1 回の繰り返し（tile）の画素、`matrix` は tile の単位の正方形を見えている頁へ写す行列で、tile は単位の正方形の辺で平面を埋める。alpha と blend は fill と同じ。rasterizer は覆う画素ごとに tile の座標へ戻し、周期で折り返して bilinear で sample する（縮小の時は画像と同じく 4×4 まで平均）。app は display list を `pdf_display_list_rasterize()` でしか読まない（Notes・PDF Viewer・preview、source で確かめた）ので、item の型の追加は app に影響しない。
- **cell の解像度（M-7 の決め）**: tile は display list を作る時に**一度だけ**画素にし、同じ display list の中で使い回す（鍵は pattern の object と pattern 空間の行列の線形の部分）。
  - cell の content が 1 枚の画像を描くだけで、その画像の単位の正方形が step の矩形 `[0, XStep] × [0, YStep]` にちょうど重なる時は、その画像の画素がそのまま tile（倍率に依らず正確。Notes の粒はこの形）。
  - 他の cell（文字・線・shading・入れ子の pattern）は、cell の content を別の display list に描き、step の矩形の大きさを見えている頁で 8 px/pt にした解像度で rasterize して tile にする（BBox が step より大きい時は隣の写しも描く、写しは 64 まで）。tile は 1 Mi px・各辺 2048 px を上限に解像度を下げる（拡大した viewer では粗く見える。制限）。pattern の入れ子は 2 段まで（超えたら SKIPPED）。
  - tile の画素は display list の画素の数（`PDF_DISPLAY_PIXELS_MAX`）に一度だけ数える。
- 古い reader（今の libpdf）は tiling pattern を飛ばす（`content.c` の `set_pattern()`）ので、鉛筆は下塗りだけの薄い単色の線になる（p001 §5.2、review-2 §1 S1 で確かめ済み）。

## 4. 受け入れ

| # | 条件 |
| --- | --- |
| A1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk`）の libpdf・Notes・libpdf を使う app（pdfviewer・preview）で warning 0。Linux の host の `Makefile.linux` の libpdf の build（M-12、FreeBSD は native の build ができなければ未実施と記録） |
| A2 | `pdf_outline_stroke()` の出力が main `f9077504e` の outline.c と bit 同一（乱数と定形の stroke 多数） |
| A3 | 範囲の輪郭: 隣の区間の継ぎ目の 2 角が bit で同じ。自分と重ならない stroke で、区間の nonzero の面積の和が全体と一致し、区間どうしの重なりが 0（行ごとの span の長さで、許容差は面積の 1e-6 相対）。cut を捨てる場合（gentle な頂点が無い）・最初と最後の区間の cap・seam の辺の index が §1.2 の約束のとおり。ループの stroke では面積の和が全体より大きいこと（制限の記録）。不正な入力は EINVAL |
| A4 | `q/Q` の後の同じ alpha の fill が正しい `gs` を持つ（content の text で確かめる）。Multiply を使わない文書の ExtGState に `/BM` が無い |
| A5 | 書いた PDF（Multiply の水彩の形、粒の鉛筆、Flate の content、高さの違う 2 頁、回転した他の PDF の上の頁）を `qpdf --check` が通し、libpdf・`pdftoppm`・Ghostscript の raster が互いに近い（平均の差と差 64 超の画素の割合の閾値） |
| A6 | 粒の向き（M-6）: 粒で塗った大きな矩形の、見えている頁の決まった点の画素が、粒の texel から計算した値と合う（libpdf・pdftoppm・gs、新しい頁 2 つと `/Rotate 90` の頁）。pattern の fill に `/ca` が掛かる |
| A7 | Flate の content の頁の reader の hash が writer の hash と一致。flate を有効にしない文書の content に `/Filter` が無い |
| A8 | 鉛筆 200 本相当（各 3 区間、下塗りと pattern）の頁で `PDF_DISPLAY_LIMITED` が立たず、raster の時間と item・点・画素の数を記録（L-16） |
| A9 | Save Clean Copy（M-13）: 3.0 相当の文書（粒・Multiply・Flate）の clean copy を `qpdf --check` が通し、元と libpdf・pdftoppm の raster が近い |
| A10 | `pdftoppm -r 600` で、同じ alpha の 2 区間に分けた鉛筆の形と 1 つの形の差の画素の数を記録（継ぎ目の髪の毛の線、M5） |
| A11 | 既存の libpdf の host 試験（`plan/ws079/tests/run-pdf-writer.sh`・`run-pdf-reader.sh`・`run-pdf-render.sh 100`・`run-pdf-update.sh`、`plan/ws175/tests/run-host-clean.sh`）が通る |

試験: `plan/ws216/tests/run-pdf-tools.sh`（`host-pdf-tools.c` を plain・ASan・UBSan で）。出力は worktree の `build/ws216-p002-host/`。

## 5. 実行の記録

（実装の後に書く）
