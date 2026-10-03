<!-- awesome-plan project=zedbsd record=ws074p074 -->

# ws074-p074: block の幅の intrinsic の keyword（max-content・min-content・fit-content）

Phase ID: `ws074-p074`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p060
由来: 2026-09-29 main の判断（Amazon のトップの最初の card の列、横の carousel `.gwm-window-wrapper { width: max-content }`）。順は
p072 の後、p075・p037 の前。

## 範囲

`width: max-content`・`min-content`・`fit-content`（`-webkit-`・`-moz-` の別名）。carousel を正しく出すのに要った次も含めた:
測定の中の % の幅の置換要素、`calc()` の中の `min()`・`max()`・`clamp()`（p061 の残り）、横に並ぶ float の max-content、
formatting context を作らない box の測定の float。

## 実装

- CSS（`css/css.h`・`values.c`）: 長さの keyword に `CSS_UNIT_MAX_CONTENT`・`MIN_CONTENT`・`FIT_CONTENT`。
- block（`layout/block.c`）: 幅がこれらの box は `block_intrinsic`: 内容の max-content（`layout_max_content`）、min-content（幅 0 で
  layout した内容の幅）、fit-content（残りの幅を二つの間に挟む）を測り、その内容の幅を px として layout（style は keyword に戻す、
  border-box なら枠を足す）。
- 測定（`layout/position.c`）: `layout_max_content` は自分の block formatting context の中で測る（前は formatting context を作らない
  box の測定の float が外の context に残り、本番の float を押し下げていた）。min-content の測定も同じ。`layout_content_width`: % の
  幅の置換要素は測る幅の % ではなく画像の大きさ（control は natural width）。block の子の float と行の中の float は横に並ぶ幅
  （左の float の右の margin の端の最大と右の float の和）。
- calc（`css/internal.h`・`values.c`・`cascade.c`）: sum に入れ子の `min()`・`max()`・`clamp()` を 1 つと係数（`nested`・
  `nested_factor`、parse の arena）。`calc((clamp(1167px, 100vw, 1330px) - 32px - 35 * 8px) / 36 * 7.5 + 6.5 * 8px)` のような
  Amazon の変数が値になる。cascade は入れ子を px で評価（% は viewport の幅で、top level の min・max と同じ近似）。
- 試験: `tests/pages/intrinsic.html`（新、golden 4 つ）: max・min・fit・`-webkit-fit-content`、狭い block の中の fit、border-box、
  inline-block の max-content の list、max-content の float、入れ子の clamp の calc の幅の float の max-content の list、100% の画像の
  max-content。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: 新しい指摘 0（既存の 1 件）。
- 回帰（plain と ASan）: golden 68/68（intrinsic の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、host-link 22/22、
  host-position 19/19、host-text 20/20、host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8、host-relayout 147/147。ASan の `--render` で
  Amazon の 3 つの capture に報告なし。
- `chrome-boxes.py intrinsic.html`（800x600）: 26 のうち 25 が 1px 以内（違う 1 つは文字の幅 1.5 px）。`render-compare.py`: 98.31%。
  注意: `min-content` の語「min-content:」は Chromium が hyphen の後で改行するが私たちはしない（改行の規則の差、この Phase の外）
  ので、試験の文を変えた。
- Amazon（2026-09-28 23:52 の capture、1280x900）:

  | page | p072 の後 | p074 の後 |
  | --- | --- | --- |
  | トップ | 画素 34.72%、ink 24.43% | **画素 79.12%、ink 75.06%** |
  | 検索 | 画素 77.16%、ink 34.68% | 画素 77.16%、ink 34.68% |

  トップの最初の card が Chromium と同じく 1 行の横の carousel（card の幅も同じ）になり、footer の位置も揃った。card の中の
  高さ・格子は container query の単位（p075）。
- guest（QEMU の Venus）: `browser-page.sh intrinsic.html` status 0。live の `https://www.amazon.co.jp/`（取得 1 回）: 30 s で
  header と 1 行の card の carousel（写真）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p074-20260929-intrinsic.png`、`p074-20260929-amazon-top-local-noscript.png`・`…-search-…`。
- guest（QEMU）: `p074-20260929-guest-intrinsic.png`、`p074-20260929-guest-amazon-top-30s.png`。

## 未実施・残り

- 実機は未実施。
- `min-width`・`max-width`・`height` の intrinsic の keyword（今は無視）、`fit-content(<length>)`、hyphen の後の改行、
  calc の中の 2 つ以上の min・max・clamp、入れ子の % の正確な基準。
