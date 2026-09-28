<!-- awesome-plan project=zedbsd record=ws074p061 -->

# ws074-p061: CSS の値（var・calc・@media・@supports・論理 property）

Phase ID: `ws074-p061`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p007、p068

## 範囲（amazon-goal.md §4 の 3。selector と pseudo-element は p069）

`var()` と custom property、`calc()`・`min()`・`max()`・`clamp()`、`!important` の確認、`@media`（幅・高さ・range の形・
`prefers-*` 等）、`@supports`、論理 property、`<link media>`・`@import … media`。

## 設計と実装

- `css/media.c`（新）: media query list を test（width・height・aspect-ratio・orientation・resolution と定数）の列に読み、
  style の計算の時に viewport で評価する（sheet は一度だけ parse し、どの大きさでも使う）。media type は all・screen が真。
  `not`・`only`・`and`、`(min-width: …)`、`(width >= …)`・`(400px <= width <= 800px)` の range の形、px・em・rem、比、dppx・dpi。
  この browser は screen、fine な pointer、hover あり、1 dppx、light、reduced-motion なし、`color` は 8。知らない feature は偽。
  入れ子の `@media` は親の list を指す（全部が真のとき）。
- parser（`css/parser.c`）: rule の読み取りを `parser_rules`（入れ子の block にも使う）と `parser_at_rule` に分けた。`@media` の
  block の rule は list を持つ（`css_rule.media`）、`@supports` は parse の時に評価（`not`・`and`・`or`、宣言は property と値が
  読めれば真、`selector()` は真）、`@layer` の block はそのまま（layer の順は保たない）、他の at-rule は飛ばす。`@import` の
  media を記録。`--name: tokens` は custom の宣言（token のまま）、値に `var()` を含む宣言は pending（property の番号と token）。
- 値（`css/values.c`）: 値の parse に arena を渡す（`struct css_parse`）。`calc()`（`-webkit-calc`、入れ子の括弧と calc、`+ - * /`）・
  `min()`・`max()`・`clamp()` を長さの property（寸法・margin・padding・border の幅・offset・font-size・line-height・
  background-size）で `struct css_calc`（px・%・em・ex・rem・vw・vh・vmin・vmax の和、最大 3 つ）に。数だけの calc は
  line-height の数か 0。単位 `vmin`・`vmax`、`cqw`・`cqi`（vw として）・`cqh`・`cqb`・`svw` 等。論理 property（inline は
  left・right、block は top・bottom の横書きの左から右）: `margin-inline-*`・`padding-block-*`・`inset-*`・`inline-size` 等、
  `margin-inline`・`padding-block`・`inset`・`inset-inline` 等の shorthand、`border-inline-start` 等。
- cascade（`css/cascade.c`）: 整列の後に要素の custom property（親の list の前に自分の宣言、後の宣言が先に見つかる）を作り、
  各値の `var(--x, fallback)` を置き換えて engine の arena に保つ（深さ 16 で循環を止める、置き換えられない値は無効）。
  pending の宣言は custom で置き換えて property として parse し、同じ順位に展開（無効なら `unset`）。calc は font-size・
  root・viewport で px に、`calc()` の % は `css_length.offset`（px）付きの % として layout に渡す（`block.c`・`position.c`・
  `replaced.c`・`box.c` で offset を足す）。`min()`・`max()`・`clamp()` の % は viewport の幅で測る（近似）。sheet の media
  （`<link media>`・`<style media>`・import の media の鎖）と rule の media を照合の時に評価。
- page（`page/sheets.c`）: `<style>`・`<link>` の `media` 属性を engine の arena に parse し、import の media と鎖にして
  `css_engine_add_parsed` に渡す。style の dump は calc の offset を `50.00%-20.00` のように出す。
- 試験: `tests/pages/values.html`・`values-print.css`・`values-screen.css`（新、golden 4 つ）。

## 確認（host は Debian の cc。guest は未実施、p068 と同じ理由。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変更した source）: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain と ASan）: golden 40/40（values の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、run-js-tests 7/7、
  run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、host-base・heap・interp・link・number・
  object・position・text 全て 0 failed。ASan の `--render` で Amazon の 3 つの capture に報告なし。
- `render-compare.py values.html`（800x600）: 95.78%。全ての行の位置・幅・色が Chromium と一致（違いは glyph の縁）:
  var と fallback、再定義の継承、var の中の var の shorthand、無効な var で継承、`calc(100% - 2 * 10px)`・入れ子、`min`・
  `clamp`、`calc(1rem + 2px)`・`calc(1.5)`、幅の media・range・入れ子、`@supports`、`!important` が後の rule と style 属性に
  勝つ、論理 property、`25vmin`、print の link を使わない。
- Amazon（p068 と同じ capture の `*-local-noscript.html`、1280x900）:

  | page | p068 の後 | p061 の後 |
  | --- | --- | --- |
  | トップ | 画素 14.97%、ink 7.86% | 画素 23.51%、ink 16.64% |
  | 検索 | 画素 75.75%、ink 19.47% | 画素 73.65%、ink 18.82% |

  トップは card の格子の色・大きさ・文字が Amazon の変数で出るようになった。検索は変数で幅の決まる部品が出たが、flex・
  inline-block（p035・p060）が無いので横に並ばず、一致は横ばい。検索の `--render` は host で 1.40 s。

## 写真（`/home/awe/zedBSD-rpi4/.claude/worktrees/agent-ae19962dd0a453495/build/ws074-shots/`、host の証拠）

- `p061-20260928-values.png`（私たち | Chromium | 違い）、`p061-20260928-amazon-top-local-noscript.png`・
  `…-search-local-noscript.png`。

## 未実施・残り

- guest の窓の試験（worktree の image に target の sysroot が要る。main の許可待ち、p068 を参照）。
- `@container`（Amazon に 37）、`@layer` の順、`@font-face`（p070）、`env()`、`attr()`、色の `var()` 以外の関数
  （`hsl()`・`color-mix()`）、calc の中の min・max、`min()` 等の % の正確な基準、number の property（`opacity`・`z-index`・
  `flex`）の calc、custom property の `initial` と `@property`、media の `or`。
