<!-- awesome-plan project=zedbsd record=ws074-p235 -->

# ws074-p235: position: sticky（ページのスクロール）と、子を持つ箱の transform: scale

Status: cleared（2026-10-18 Q1: Status の行が無かったので写した。元: cleared（host、2026-10-15 B1。sticky・子つき scale。固定 WPT 483→485、css-position 51→54、css-transforms 221→226（偶然 pass 4 件が後退）。QEMU・実機は未実施。flex の aspect-ratio は未着手））

B1 (2026-10-15)。ユーザーの指示「Interop 2024, 2023 の、よく使われるレイアウトの部分を選んで改善」の続き。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p234 の続き。

## 実装

1. `position: sticky`（ページのスクロールに対してだけ。inner scroll container は engine に scroll が無いので対象外）
   - 新規 `layout/sticky.c`: layout の最後に sticky の箱（block）を tree 順に集め、scrollport（viewport をスクロール位置で。clip する祖先の中なら、その padding box を scroll 0 として）を top/right/bottom/left の offset で縮めた範囲に margin box が入るよう、箱とその中身（abspos の子を含む、fixed は除く）を動かす。動かす量は親の content box の中に収める。bottom を先、top を後に適用し top を優先する。各箱は動かした量を覚え、**layout をやり直さず**別のスクロール位置で再適用できる（`layout_sticky_apply`）。
   - `layout_is_positioned` が sticky を positioned とする（abspos の containing block・重なり順は仕様どおり、後の sticky が前より上に描かれる）。
   - `page_sticky_sync`（page.c）と `view_update`（view.c）: スクロール位置が変わって描く時、sticky の箱を動かして display list を作り直す（sticky が無い page は何もしない）。
   - 試験: [host-sticky.c](../tests/host-sticky.c) と [pages/sticky-page.html](../tests/pages/sticky-page.html)（scroll 0・300・900 の header(top:30)・side(top:0)・footer(bottom:10) の色を画素で確認。header は 0→30 に、footer は 1200→540 に引かれ、スクロール 300 で side が上端に貼り付く）。
2. 子を持つ箱の `transform: scale`
   - `paint/list.c` `list_scale`: 箱の display item（背景・枠・文字・画像・clip）を transform-origin を中心に拡縮する。文字は pen の位置を x の倍率、font の大きさを y の倍率で。子も行も無い箱は p233 の layout 側（border は 1px 未満にならない）のまま。positioned の子孫は自分の turn で描かれ、ここでは拡縮されない（制限）。縦書きの text は拡縮しない。
   - 試験: [pages/scale-children.html](../tests/pages/scale-children.html)（0.5 倍の枠＋文字、origin 中央の 1.5 倍）。結果 PNG: [sc.png](../../../build/b1p235/sc.png)。

## 前後の数（同じ WPT revision `2d66b9b7…`、before は main の 449b91dc1 の binary、after は今回）

| 範囲 | 前 pass | 後 pass | PASS→FAIL |
| --- | ---: | ---: | --- |
| css/css-position/sticky（20 件） | 4 | 4 | 0 |
| css/css-position（132 件） | 51 | **54** | 0 |
| css/css-transforms（709 件） | 221 | **226** | 4（下記） |
| 固定 WPT 1,455 ID | 483 pass・callback 10,983 | **485 pass**・callback 10,987 | 0（ID 単位） |

- 固定 WPT の増分は `css-grid/alignment/grid-{column,row}-axis-alignment-sticky-positioned-items-002`。
- sticky の reftest 20 件は残りが inner scroll container の `scrollTop`・`scrollLeft`（onload で設定）を前提にしていて、今回の対象（ページのスクロール）では測れない。数は 4/20 のまま。
- css-transforms の PASS→FAIL 4 件（`transform3d-perspective-003/004`・`transform3d-scale-005/006`）は `rotateX` 等（未実装）を含み、scale が子つきの箱で無視されていたため偶然 pass していた物。scale が効くようになり、rotate が無いので ref と合わなくなった。
- yahoo.co.jp live（1024x3400）: [前 | 後 | Chromium](../../../build/review/b1-yahoo-p235.png)（上半分 [crop](../../../build/review/b1-yahoo-p235-top.png)）。sticky・子つき scale を使う箇所が目に見えて無く、前後の差は広告と時刻の更新の範囲。Chromium は request ごとに広告・ポップアップが違う。実 site での効果の実証は無い（headless の静止画のため）。

## 確かめ（host）

- build warning 0。host JS 17/17、DOM 23/23、http 15/15、loader 11/11（`--program build/ws074-host/plain/host-loader`）、fetch blocking・`--async` 38/38、CSS rule model 51/51、Grid 23/25（`abspos-static`・`auto-track-css` は p232 から既存）、host-sticky ok。Acid3 score 100・差 0、Acid2 差 873（不変）。
- 未実施: QEMU・実機、GPU（Vulkan）経路でのスクロール中の sticky 表示、項目 3（flex item の aspect-ratio、31 件）。

## 残り

- inner scroll container の scroll（`overflow:auto` の `scrollTop`）と、その中の sticky。
- 子つき box の rotate・skew、positioned の子孫を含む scale、scale 後の hit test・`getBoundingClientRect`。
- flex item の aspect-ratio（p234 の残り 31 件）。
