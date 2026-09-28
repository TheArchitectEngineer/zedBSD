<!-- awesome-plan project=zedbsd record=ws074p052 -->

# ws074-p052: CSS の背景画像（`background-image`・repeat・position・size）

Phase ID: `ws074-p052`（2026-09-28 main が p021 から分けて割り当て）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p021

## 範囲（正常系のワンパス）

- CSS（`css/values.c`・`cascade.c`・`css.h`・`internal.h`）: longhand の `background-image`（`url(...)` の URL token と
  `url("…")`、`none`。URL は atom）、`background-repeat`（1 つか軸ごとの 2 つ。space・round は repeat として描く）、
  `background-position-x`・`-y`、内部の `background-size` の幅と高さ。shorthand の `background-position`（1〜2 の値、
  left・center・right・top・bottom と長さ・%）、`background-size`（contain・cover・1〜2 の長さ・%・auto）、`background`
  （色・画像・repeat・position・`/ size` を順不同、書かれない値は初期値）。CSS 全体の keyword（inherit 等）も各 longhand へ。
- layout: box は style の背景画像の URL を page に引いてもらう（`layout_url_lookup`、`page_image_by_url`: page の location で
  解決し、初めてなら同期で取得と decode。p050 で非同期へ）。p021 の表を entry への pointer の表にした（layout の途中で表が
  伸びても、先に渡した bitmap の場所が変わらない）。
- paint（`paint/list.c`）: 背景色の上、border の下に背景画像。位置の基準は padding box、描く範囲は border box（その clip）。
  大きさ: contain・cover、長さ・%（padding box に対して）、auto は比か画像の大きさ。位置: % は余りに対して、長さは端から。
  repeat の軸は描く範囲を覆うまで並べる（1 つの画像で最大 16384 枚）。root・body の背景（色か画像）は canvas のもの:
  root の padding box を基準に canvas 全体に描く。描画は p021 の `PAINT_IMAGE` のままなので CPU と GPU の両方で描ける。
- 試験: `images/backgrounds.html`（repeat・repeat-x・repeat-y・no-repeat の位置 3 種・contain・cover・長さ・50% auto・GIF・
  見つからない画像・body の canvas の tile）、`make-test-images.py` に `tile.png`、`browser-p052.sh`（Venus の窓と guest の
  GPU・CPU の比較）。

## 受け入れ

1. amd64 の build（warning 0）、変えた file の style-check 0（前からの 1 件を除く）。
2. host: backgrounds.html の CPU と GPU が一致、Chromium と比べる。前の試験（golden の dump、GPU の比較）が下がらない。
   ASan で報告なし。
3. guest（Venus）: 窓に backgrounds.html、guest の GPU と CPU が一致。前の窓の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- host: backgrounds.html（900x640）の CPU と GPU が一致（最大差 2、2 超は 0 pixel）。images.html・first.html も一致。
  Chromium との比較 92.66% が channel 差 16 以内（差は font の幅による文字の位置、Chromium の縮小の補間）。写真
  `/home/awe/zedBSD-rpi4/build/ws074-shots/p052-20260928-backgrounds.png`（ours | Chromium | 差）。golden の dump 28/28。
  ASan と UBSan で backgrounds.html の描画と images.html の style の dump に報告なし。
- guest（Venus）: `browser-p052.sh` status 0（READY、ERROR なし、guest の GPU と CPU が一致: 最大差 2）。写真
  `p052-20260928-window-backgrounds.png`。前の窓の試験: `browser-p021.sh`・`browser-p045.sh` status 0。
- boot test: PASS、`p052-20260928-boot-login.png`。
- build: amd64 の image（browser の warning 0）。style-check: `css/values.c`・`css/css.h`・`css/internal.h`・`paint/list.c`・
  `page/images.c`・`layout/box.c`・`layout/layout.h` 0、`css/cascade.c` は前からの 1 件（`cascade_font_size` の段落の comment、
  この Phase の前の HEAD にもある）だけ。

## 後回し（follow-up）

- 3〜4 の値の background-position（`right 8px bottom`。今は宣言ごと捨てる）、`background-origin`・`-clip`・`-attachment`、
  複数の層（カンマ区切り）、gradient（`linear-gradient` 等、p038）、`image-set()`。
- space・round の repeat の本当の配置。拡大・縮小の補間（p021 と同じ）。
- 外部の style sheet（`<link>`、p009）の URL はその sheet の場所に対して解決する（今は document の場所）。
- 背景画像の取得の非同期化（p050）。
