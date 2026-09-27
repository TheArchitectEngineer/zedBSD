<!-- awesome-plan project=zedbsd record=ws074p012 -->

# ws074-p012: 描画の最小（ワンパス）: display list と CPU の参照の描画

Phase ID: `ws074-p012`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

正常系のワンパス: layout から display list（`paint/paint.h` の型が GPU と CPU の描画の共通の定義）を作る（canvas の色、block の
背景、border（どの style も solid）、text の run、下線）、CPU の参照の描画（`paint/software.c`: 矩形は画素の面積の被覆、glyph は
pen の位置を画素に丸めて coverage の bitmap）、`--render --output=OUT.ppm FILE`、`--dump=paint`、Chrome の screenshot との比較。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check（paint/・main.c・page/page.c）0 件。
2. host の plain・ASan で golden（dom・style・layout・paint）が一致し、`--render` が ASan で落ちない。
3. guest の `--render` の画像が host の画像と一致（libm・libc の差が無い）。
4. Chrome の screenshot との画素の一致の率（channel の差 ≤ 16）を記録し、画像を main に送る。
5. boot test。

## 結果（2026-09-27）

cleared。

- 書いたもの:
  - `paint/paint.h`: display list の型（`PAINT_RECT`: 位置・大きさ・色、`PAINT_TEXT`: 原点・baseline・font・色・glyph の列
    （code point と pen の位置））。座標は layout の単位（1/64 px）の文書座標で、描画が scroll を引く。
  - `paint/list.c`: CSS 2 の描画の順を normal flow に絞った walk（canvas → block ごとに背景・border → 子 → 行の text）。canvas の色は
    root の背景、無ければ body の背景（その box の背景は二度は描かない）。border は 4 つの矩形（上下は全幅、左右はその間）。
    visibility: hidden は自分の描画を省く。下線は baseline の下に大きさの 1/10（整数の px、1 以上）、太さは 1/16（1 以上）。
    `paint_dump`（`--dump=paint`）。
  - `paint/software.c`: CPU の参照の描画。矩形は画素の正方形との重なりの面積を被覆にし、glyph は pen の位置と baseline を最も近い
    画素に丸めて bitmap を置く。色は straight alpha で重ね、canvas は不透明（半透明の canvas は白の上に）。PPM（P6）の書き出し。
    GPU の描画（p014）はこの被覆の規則に合わせる。
  - `page_paint`、`main.c` の `--render --output=OUT.ppm FILE`（viewport の大きさ、文書の先頭から）と `--dump=paint`。`--dump=` の
    名前は表で引く。page の heap の stack の底は、page を使い続ける呼び出し元の frame を渡す。
  - p011 の後回しの 1 つを直した: **list の marker の付け替え**。list item の子が block のとき（`<li>First<ul>…</ul></li>`）、marker を
    最初の行を持つ子孫の block（anonymous を含む、入れ子の list item の手前まで）に付ける（前は描かれなかった）。
- 試験の道具: `plan/ws074/tests/render-compare.py`（自分の `--render` と Chromium の screenshot、並べた画像と差の画像、channel の差
  ≤ 16 の画素の率）。golden に `paint` を足した（`golden-dumps.sh … paint`）。
- 試験:
  - host plain・ASan（UBSan 込み）: golden（dom・style・layout・paint）8/8。ASan で `--render`（2 page × 800x600・520x400）が落ちない。
    host-base 2038/2038、host-heap 31/31。`blocks.layout` の golden は marker の「1.」の行が 1 つ増えただけ。
  - Chrome との画素の一致（差 ≤ 16）: `first.html` 800x600 で 95.17%、`blocks.html` 800x900 で 95.80%、520x400 では 90.27%・93.98%。
    差は glyph の縁（Chromium は glyph を小数の位置に置く。自分は画素に丸める）、太字（Chromium は Inter の本物の bold、自分は
    1 px 太らせる）、斜体が無い（`<em>`・`<i>`）、bullet（Chromium は図形、自分は glyph）。box の位置は p011 の比較で一致。
  - amd64 の build: warning 0。
  - guest（plain、QEMU）: `--dump=dom|style|layout|paint` の 8 file が golden と一致。`--render` の PPM 2 枚が host の PPM と
    byte 単位で同一。
  - boot test: PASS。
  - 実機: 未実施。
- 画像（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）: `p012-20260927-render-first.png`・`p012-20260927-render-blocks.png`
  （左が自分、中が Chromium、右が差 ×4）、`p012-20260927-render-{first,blocks}-ours.png`、guest の描画
  `p012-20260927-guest-render-{first,blocks}.png`、`p012-20260927-boot-login.png`。
- commit: `a9512f6f`（code・試験）と、この記録の commit。

## 後回し（follow-up）

- glyph の小数の位置（1/4 px の glyph の cache、design.md §7）、本物の太字・斜体（可変 font の軸か別の file）、kerning。
- border の style（dashed・dotted・double・groove・ridge・inset・outset）、角の斜めの継ぎ目、角丸、box-shadow、背景画像、gradient。
- clip（overflow）、opacity・transform の group、positioned・z-index の stacking の順（p013 の後）。
- 下線の位置と太さを font の `post` の表から（libtruetype への関数の追加が要る）。list の bullet を図形で描く。
- `--render` の文書全体（viewport より下）と scroll の位置の指定。
