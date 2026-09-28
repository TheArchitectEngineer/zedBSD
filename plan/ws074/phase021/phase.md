<!-- awesome-plan project=zedbsd record=ws074p021 -->

# ws074-p021: browser の画像: `<img>`、JPEG・PNG・GIF、画像の表、両方の描画

Phase ID: `ws074-p021`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p016、p020、p051、ws071-p010（すべて cleared）

2026-09-28 に main の承認で分けた: CSS の背景画像（`url()`・repeat・position・size）は [ws074-p052](../ws.md)。

## 範囲（正常系のワンパス）

- `image/`（新、engine。Wayland・shell に依存しない、design.md §19）: 先頭の bytes で種類を判定（JPEG・PNG・GIF）、
  `libjpeg-compat`（RGB・gray・CMYK。Adobe の反転した CMYK は C×K/255）、`libpng-compat`（simplified API で RGBA）、
  `libgif-compat`（最初の image を logical screen の透明な canvas に、local／global の color map と透明色）で
  0xAARRGGBB（straight alpha）の bitmap へ。bitmap は program の間で一意の serial を持つ。
- `page/images.c`（新）: layout の前に DOM の `<img src>` を page の location で解決し、同期で取得（`page_fetch`: file・
  data・http・https）と decode。location ごとの表（失敗も覚えて取り直さない）。p050 の非同期の loader が同じ表を埋める。
  `page_load_file` は page の base を絶対 path にする（相対 path の page の画像が解決できなかった）。
- layout: `<img>` は replaced box。inline なら `LAYOUT_REPLACED`（行の中の 1 つの piece、margin box の下端が baseline、
  前後で改行できる）、block・float・absolute なら内容の無い block。大きさ（`layout/replaced.c`、CSS 2 §10.3.2・§10.6.2）: style の
  width・height（px、width は % も）、無ければ HTML の width・height 属性、無い辺は画像の比か画像の大きさ、max-width・max-height は
  比を保って縮める。画像の無い `<img>` は属性の大きさの空の箱。`layout_block` の margin・auto の margin を
  `layout_box_model`・`layout_auto_margins` として共有。`--dump=layout` に `replaced` と画像の大きさ。
- paint: `PAINT_IMAGE`（内容の箱に画像を伸ばす。各 pixel は中心の下の texel、端は矩形と同じ被覆）。inline の replaced は
  背景・border・画像、block は block と同じ。CPU（`software.c`）と GPU（`vulkan.c`: glyph と共有の atlas を 2048x2048 に、
  image の置き場は serial で引く表、fragment shader の kind 2、instance に source と texels の vec4 を 2 つ足した、
  `shaders.h` を再生成）。
- 試験: `make-test-images.py`（自前で描いた画像と `images/images.html`、commit しない）、`browser-p021.sh`（Venus の窓、
  link の中の画像の click、guest の GPU と CPU の比較）。guest の image に `/usr/share/browser-images/`。

## 受け入れ

1. amd64 の build（warning 0）、新しい file と変えた file の style-check 0。
2. host: `images.html` を CPU と GPU（lavapipe）で描き、比較が一致（channel ≤ 2、0.1% 以内）。Chromium との比較を見る。
   前の試験（golden の dump、GPU の比較、host の単体）が下がらない。ASan で image の page を描いて報告なし。
3. guest（Venus）: 窓に画像の page、link の中の画像の click、GPU と CPU の一致。前の窓の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- host: `images.html`（900x1150）の CPU と GPU が一致（最大差 1、0 pixel が 2 超）。全 page（7 + images）の GPU の比較も一致。
  Chromium との比較 90.45% が channel 差 16 以内（差は主に font の幅の違いによる行の位置、Chromium の縮小の補間、壊れた画像の
  alt の表示）。写真 `/home/awe/zedBSD-rpi4/build/ws074-shots/p021-20260928-images.png`（ours | Chromium | 差）。
- golden の dump 28/28（`first.layout` は WS078 の改名の sed が text だけ直して幅を古いまま残していた 1 行を直した）。
  host の単体: host-position 19/19、host-link 22/22、host-text 20/20。ASan と UBSan で image の page の CPU・GPU の描画に
  報告なし（lavapipe の 112 byte の leak は前からで、画像の無い page でも出る）。
- guest（Venus）: `browser-p021.sh` status 0（READY、link の中の画像の click で LINK・NAVIGATE、ERROR なし、guest の GPU と CPU が
  一致: 最大差 1）。写真 `p021-20260928-window-images.png`・`p021-20260928-window-image-link.png`。前の窓の試験:
  `browser-p045.sh` status 0、`browser-p014.sh` status 0（first・blocks の guest の GPU の比較も一致）。
- boot test: PASS、`p021-20260928-boot-login.png`。
- build: amd64 の image（新しい file の warning 0）。style-check: `image/`、`page/images.c`、`layout/replaced.c` と変えた
  layout・paint・page の file 0。

## 後回し（follow-up）

- 大きな画像: atlas（2048x2048）に入らない画像は GPU で描かない（CPU は描く）。画像ごとの device-local の texture にする。
- 縮小・拡大の補間（今は最も近い texel。Chromium は bilinear 等）。
- 壊れた・見つからない画像の alt の文字と icon（今は大きさだけの空の箱）。
- `<img>` の `srcset`・`sizes`・`<picture>`、`object-fit`、`loading=lazy`、decode の非同期（p050 と一緒に）。
- GIF の animation、EXIF の向き（`jpeg_save_markers` は p020 で済み）、color profile。
- 画像の表の上限と追い出し（今は page の間ずっと持つ）。
