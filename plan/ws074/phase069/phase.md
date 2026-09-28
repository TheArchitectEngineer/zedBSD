<!-- awesome-plan project=zedbsd record=ws074p069 -->

# ws074-p069: selector と pseudo-element（::before・::after）

Phase ID: `ws074-p069`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p068、p061

## 範囲（amazon-goal.md §4 の 4）

`:not()`・`:is()`・`:where()`・`:has()`（よくある形）、`:root`、構造の pseudo-class、form の状態の pseudo-class、属性 selector
（既存）、`::before`・`::after` と `content`。

## 設計と実装

- parser（`css/parser.c`）: selector list を最上位の comma だけで分ける（関数の引数の comma で切らない）。関数の pseudo-class
  は引数を読む: `:not()`・`:is()`（`:matches`・`:-webkit-any`）・`:where()` は selector list（特異性は最大の引数、`:where` は 0、
  読めない引数は `:not` なら rule を捨て、`:is`・`:where` は一致なし）、`:has()` は相対 selector（先頭の `>`・`+`・`~`、無ければ
  子孫、最初の compound の combinator に置く）、`:nth-child()`・`:nth-last-child()`・`:nth-of-type()`・`:nth-last-of-type()` は
  an+b（odd・even・`2n+1`・`-n+3`・`3n + 2` 等の token の分かれ方を ASCII に綴って読む。`of S` は未対応）。名前の pseudo-class に
  `:first-of-type`・`:last-of-type`・`:only-of-type`・`:disabled`・`:enabled`・`:checked`・`:placeholder-shown`・`:required`・
  `:optional`・`:defined`・`:scope` を追加。`:hover`・`:focus` 等の状態は一致しない（静止した page を描く）。`::before`・`::after`
  （と単一 colon の `:before`・`:after`）は pseudo-element の simple selector、他の pseudo-element（`::placeholder` 等）は索引から除く。
- cascade（`css/cascade.c`）: selector の照合に anchor（`:has` の要素）を足し、相対 selector の最初の compound が anchor と
  combinator の関係にあるかを見る。`:has()` は子孫（`+`・`~` なら後の兄弟とその子孫も）を歩く。要素の計算の時、::before・
  ::after の selector で一致するものがあれば `style->pseudo_elements` に印（宣言は要素に入れない）、layout は印のある時だけ
  `css_engine_compute_pseudo`（要素の style から継承、style 属性は使わない）を呼ぶ。`content` property（`none`・`normal`、
  文字列と `attr()` の並び、counter・quote・url は何も足さない）。
- layout（`layout/box.c`・`block.c`）: 要素の box の最初と最後の子に ::before・::after の box（display で inline か block、
  absolute・fixed・float も要素と同じ）と content の text の box。node を持たない box。clear を持つ空の block が float の下へ
  押されず margin の畳み込みで素通りしていた既存の不具合を直した（clearfix の `::after { content: ""; display: table; clear: both }`
  と実の `<div style="clear: both">` の両方）。
- 試験: `tests/pages/selectors.html`（新、golden 4 つ）。

## 確認（host は Debian の cc。guest は未実施、p068 と同じ理由。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変更した source）: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain と ASan）: golden 44/44（selectors の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、run-js-tests 7/7、
  run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、host-base・heap・interp・link・number・
  object・position・text 全て 0 failed。ASan の `--render` で Amazon の 3 つの capture に報告なし。
- `render-compare.py selectors.html`（800x600）: 91.92%。一致: `:not`・`:is`・`:where` の特異性、`:nth-child(odd|3n+1|-n+2)`・
  `:nth-last-child`、`:first-of-type`・`:last-of-type`・`:nth-of-type`、`:has(> img)`・`:has(.flag)`・`:has(+ .next)`、
  `:disabled`・`:checked + label`・`:placeholder-shown`、`::before`・`::after` の引用符、`attr()` と文字列、block の ::after、
  単一 colon、clearfix、absolute の ::before、`@supports selector(:has(+ *))`。違い（この Phase の外）: inline 要素の背景と italic の
  face が描かれない（`em:only-of-type` の style は一致）、checkbox の印と画像の無い `<img>` の枠（p062 ほか）。
- Amazon（p068 と同じ capture の `*-local-noscript.html`、1280x900）:

  | page | p061 の後 | p069 の後 |
  | --- | --- | --- |
  | トップ | 画素 23.51%、ink 16.64% | 画素 27.89%、ink 21.30% |
  | 検索 | 画素 73.65%、ink 18.82% | 画素 75.16%、ink 19.89% |

  検索の `--render` は host で 1.43 s（`:has` を含む）。残りの大きな差は flex（header の帯、結果の格子、左の filter）と
  inline-block（p035・p060）。

## 写真（`/home/awe/zedBSD-rpi4/.claude/worktrees/agent-ae19962dd0a453495/build/ws074-shots/`、host の証拠）

- `p069-20260928-selectors.png`（私たち | Chromium | 違い）、`p069-20260928-amazon-top-local-noscript.png`・
  `…-search-local-noscript.png`。

## 未実施・残り

- guest の窓の試験（worktree の image に target の sysroot が要る。main の許可待ち、p068 を参照）。
- `:hover`・`:active`・`:focus`・`:focus-visible`・`:focus-within`（状態が変わった時の再計算が要る）、`:nth-child(… of S)`、
  `:lang()`・`:dir()`、`::marker`・`::placeholder`・`::first-line`・`::first-letter`、`content` の counter・quote・画像、
  `:has()` の範囲を狭める最適化（今は子孫を全て歩く）、inline の背景。
