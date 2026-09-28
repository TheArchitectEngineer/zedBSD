# WS079: libtruetype の輪郭の API（truetype_glyph_outline）

[design-pdf.md](design-pdf.md) §6 の 2（段階 ② の glyph）へのユーザーの決定「libtruetypeにはアウトラインを返すAPIを追加しましょう。」
（2026-09-28）の実装。libpdf が glyph を bitmap ではなく曲線で描くための入力。

## API（`include/libc/truetype.h`）

```c
struct truetype_outline_point { float x; float y; unsigned on_curve; };
struct truetype_glyph_outline {
	struct truetype_outline_point *points; unsigned point_capacity; unsigned point_count;
	unsigned *contour_ends; unsigned contour_capacity; unsigned contour_count;
	int advance; int left_side_bearing; int x_min; int y_min; int x_max; int y_max;
};
int truetype_glyph_outline(const struct truetype_face *face, unsigned glyph,
			   struct truetype_glyph_outline *outline);
```

- 単位は design units（`truetype_design_metrics()` の `units_per_em`）、y は上向き。pixel size の設定に依らない。
- 点は glyf のまま: `on_curve` が 1 は曲線上の点、0 は二次曲線の制御点。制御点が 2 つ続けば中点に曲線上の点を補う
  （TrueType の規則。libpdf はこれを PDF の三次の `c` に上げるか、二次を三次に変換して書く）。
- `contour_ends[i]` は輪郭 i の最後の点の index（glyf の endPtsOfContours と同じ意味、合成では平坦化後の通し番号）。
- `advance`・`left_side_bearing` は hmtx（hMetrics の外の glyph は最後の advance と leftSideBearing 配列の値）、
  `x_min`〜`y_max` は glyf の entry の header の box。空の glyph（space）は輪郭 0・box 0。
- 配列は呼び出し側が用意する。足りなければ `ENOSPC` を返し、`point_count`・`contour_count` に必要数を入れる
  （容量 0 で一度呼んで数を得て、確保して再度呼ぶ）。範囲外の glyph は `EINVAL`。
- 合成 glyph は平坦化する: 各 component を読み、scale・x/y scale・2x2 matrix（F2Dot14）を点に掛け、offset を足す。
  offset は `SCALED_COMPONENT_OFFSET` が立ち `UNSCALED_COMPONENT_OFFSET` が立たないときだけ matrix を掛ける
  （既定は掛けない。Windows と fontTools の既定と同じ）。`ARGS_ARE_XY_VALUES` が無い component は点の番号で合わせる
  （親の点 − 変換後の子の点）。入れ子は `TRUETYPE_COMPOSITE_DEPTH`（8）まで、平坦化後の点は 65536、component は 1024 まで
  （`ENOTSUP`）。悪意ある font の指数的な展開を止める上限。hinting の命令は読まない。
- `exports.map` に `truetype_glyph_outline` を追加。既存の bitmap の API（`truetype_render_glyph` など）は変えていない。

## 変更

- `userland/desktop/libtruetype/contour.c`（新規、coding-style の全文に従う）: 上の API。
- `userland/desktop/libtruetype/outline.c`: `glyph_range()` を library 内の `truetype_glyph_range()` にした（static を外し
  `internal.h` に宣言）。中身は不変。`exports.map` の `local: *` で外へは出ない。
- `userland/desktop/libtruetype/Makefile`・`exports.map`・`include/libc/truetype.h`。

## 検証（2026-09-28、host と build。QEMU・実機は未実施）

- host 試験 `plan/ws079/tests/truetype-outline-test.sh`: libtruetype の source を host の cc で build（`-Wall -Wextra -Werror`）し、
  plain と `-fsanitize=address,undefined -fno-sanitize-recover=all` の 2 通りで、font の**全 glyph** を fontTools 4.57.0
  （`python3-fonttools`、Debian の package を導入）の `getCoordinates()`・`hmtx`・glyf の box と比べる（座標は 0.01 以内、
  on-curve・contour ends・advance・lsb・box は一致）。各 glyph は容量 0 で `ENOSPC` と必要数、ちょうどの容量で成功、
  glyph 数ちょうどの index は `EINVAL` も確かめる。font は host の `/usr/share/fonts` を読み、commit しない。
  - DejaVuSans 6253 glyph（合成 2607）、DejaVuSerif 3528（合成 1407）、DejaVuSansMono-Bold 3316（合成 1236、scale/matrix 付き 1）、
    NotoSansBalinese-Regular 361（合成 68、scale/matrix 付き 3）: plain・ASan+UBSan とも **0 differ**。
  - 例: DejaVuSans の U+00E9 eacute は合成（3 輪郭 32 点、advance 1260、lsb 113、box (113,-29,1151,1638)）、U+01FA Aringacute は合成
    （4 輪郭 38 点）、U+0041 A は単純（2 輪郭 11 点）、space は輪郭 0。
- 既存の bitmap の試験 `plan/ws035/tests/truetype-test.c` を ASan+UBSan で host build して DejaVuSans で実行: 28 ok、`TRUETYPE PASS`。
- target build: `plan/ws035/tests/build-zdesktop-image.sh build/ws079-outline` の中で `libtruetype.so` を
  `-Wall -Wextra -Werror` で build し成功、log に libtruetype・truetype.h の warning は 0。`llvm-nm -D` で
  `truetype_glyph_outline` が export され、`truetype_glyph_range` は出ていない。image 全体の結果は下の「未実施・制限」。

## 未実施・制限

- 点の番号で合わせる component（`ARGS_ARE_XY_VALUES` 無し）は試した font に無く、fontTools とも比べていない。子の点は変換後で
  取る（FreeType と同じ）。fontTools は変換前の子の点で合わせるので、matrix 付きでこの形の glyph では両者が違いうる。
- `ROUND_XY_TO_GRID`・`USE_MY_METRICS`・phantom point・hinting は扱わない（advance・lsb はその glyph 自身の hmtx）。
- CFF（OpenType の `CFF `）の font は libtruetype が元から読まない。
- guest での動作（libpdf からの呼び出し）は未実施。libpdf の段階 ② の Phase で使う。
