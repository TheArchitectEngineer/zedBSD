<!-- awesome-plan project=zedbsd record=ws074p082 -->

# ws074-p082: getComputedStyle、offsetTop・offsetLeft・offsetParent、scrollTo・scrollBy・scroll・scrollIntoView

Phase ID: `ws074-p082`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行、2026-09-29）
依存: p031（geometry・inline style の CSSStyleDeclaration）、p081
並行: WS074 の別のエージェントが `js/`・`vm/` を進めている。この Phase は `bind/`・`page/`・`view/` だけを変え、`js/`・`vm/` は変えない。

## 由来

p081 の調べ（phase081 の表）で innerHTML の次に Amazon の script での使用が多い DOM の API: `getComputedStyle(`（外 57・inline 8）、
`offsetTop`・`offsetParent`・`offsetLeft`（35・21・13）、`scrollTo(`・`scrollIntoView(`・`scrollBy(`（18・10・3、inline 3）。
Amazon の読む property: display・visibility・position・zIndex・marginRight・marginBottom・lineHeight・height・font-size・backgroundImage・
grid-template-columns・transform（`build/p081/js/` の grep）。

## 範囲

1. **`window.getComputedStyle(element[, pseudo])`**: 読むだけの `CSSStyleDeclaration`（同じ prototype。値は読む時に計算する: live）。
   値は CSSOM の resolved value: layout の box のある要素の width・height・margin・padding は使われた値（px）、他は計算値。
   色は `rgb(r, g, b)`・`rgba(r, g, b, a)`、長さは px。`getPropertyValue`・名前の accessor・`length`・`item`。`cssText` は ""
   （Chromium と同じ）。書き込みは `NoModificationAllowedError`。pseudo の引数は見ない（要素の style）。
2. **`HTMLElement.offsetParent`・`offsetTop`・`offsetLeft`**（CSSOM View）: positioned の祖先、または td・th・table、または body。
3. **scroll**: window の `scrollTo`・`scroll`・`scrollBy`（数 2 つ、または `{top, left}`）、`Element.scrollIntoView`（真偽値か
   `{block}`: start・center・end・nearest）、`Element.scrollTo`・`scroll`・`scrollBy`（root 要素は document の scroll、他は何もしない）、
   root 要素の `scrollTop` の setter。page は scroll を document の大きさで clamp して記録し、view が窓の scroll に移す。

この Phase に無いもの: 要素の中の scroll（box は scroll しない）、横の scroll（page は横に scroll しない）、`behavior: smooth` の
動き（すぐ移る）、pseudo-element の computed style、shorthand（`margin` 等）の computed の値、`transform`（engine に無い: "none"）、
`grid-template-columns` の track の使われた大きさ。

## 設計と実装

- **host の callback**（`bind/bind.h`）: `computed_style`（要素の計算値。page が layout でき要素に box があれば box の style、
  無ければ root から cascade で計算。document の外は ENOENT）、`scroll_to`（page が document の中に clamp して記録）。`struct bind_box` に
  最初の box の使われた margin・padding を足した。
- **`bind/computed.c`（新）**: `window.getComputedStyle`。platform object（CSSStyleDeclaration の prototype）の cell が要素の object を
  持ち（trace で mark）、値を読むたびに host に尋ねる（live）。報告する property は 73（表 `computed_entries`、`length`・`item` の順）:
  box（display・position・float・clear・visibility・overflow・box-sizing）、大きさ（width・height・min/max）、margin・padding・inset・
  border（幅・style・色）、色、background-image、opacity、z-index、font（size・weight・style・family）、line-height、text-align、
  direction、white-space、vertical-align、list-style-type、border-collapse、flex・align・gap、outline、transform（"none"）、
  grid-template-columns（track の指定値）。resolved value: box のある block の width・height は使われた値（box-sizing に従う）、
  margin・padding は box の使われた値、relative の auto の inset は反対側の符号を変えた値、line-height の数は px、他は計算値。数は
  整数か 6 桁、色は `rgb()`・`rgba()`（alpha は byte に戻る最も短い小数）。
- **`bind/style.c`**: CSSStyleDeclaration の読む側（名前の accessor・`getPropertyValue`・`getPropertyPriority`・`length`・`item`・
  `cssText`）は computed の宣言を `computed.c` へ回し、書く側（`style_element` を通る全て）は `NoModificationAllowedError`。
- **`bind/geometry.c`**: `offsetParent`（CSSOM View: root・body・box の無い要素・fixed は null、それ以外は最も近い positioned の祖先、
  または body・td・th・table）、`offsetTop`・`offsetLeft`（border box から offset parent の padding box まで、parent が無いか body なら
  document の原点から、整数）。window の `scrollTo`・`scroll`・`scrollBy`（数 2 つか `{left, top}`、無限と NaN は 0）、Element の
  `scrollIntoView`（真偽値か `{block: start|center|end|nearest}`）・`scrollTo`・`scroll`・`scrollBy`（root 要素は document、他は
  何もしない）、root 要素の `scrollTop` の setter（document を scroll）。
- **`page/geometry.c`**: `page_computed_style`・`page_scroll_to`、要素の最初の box の索引（`page_box_slot` の open addressing、layout
  ごとに作り直す: `page->layout_serial`）。`page_node_box` は block（と行の外の replaced）の最初の box ならその border box を
  そのまま使い（前は layout 全体を歩いて子孫の box も union していた）、他は前と同じ union。
- **`page/page.c`・`page.h`**: `page_update_styles` を公開、`layout_serial`・box の索引の field と解放。
- **`view/view.c`**: script の scroll の要求（`page->scroll_requested`）を窓の scroll に移して描き直す（`view_changed`）。
- `libbrowser/Makefile` に `bind/computed.c`。`js/`・`vm/` は変えていない。

## 試験

- `plan/ws074/tests/dom/computed.html`（29 行、新）と Chromium 153 の expected: 使われた・計算された値（box-sizing、display:none、
  inline、色と alpha、font-family の引用、overflow の 2 値、relative の inset、%、z-index）、名前の accessor と `getPropertyValue`、
  `cssText`・`length`・`item`、書き込みの例外、live、document の外、引数の型、offsetParent（absolute・static・td・body・null の 4 種）、
  offsetTop・offsetLeft、scrollTo・scrollBy・scroll（数と dictionary、clamp、負）、scrollIntoView（start・false・center）、
  root の scrollTop・scrollTo、他の要素の scrollTo・scrollBy。

## 確認（host は Debian の cc と Chromium 153。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: 変えた file（`bind/computed.c`・`geometry.c`・`style.c`・`element.c`・
  `window.c`、`page/geometry.c`・`page.c`・`script.c`、`view/view.c`）に指摘 0。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59、host-form 28、host-link 22、host-position 19、host-text 20、
  host-relayout 161、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-dom-tests **16/16**（computed を追加）、
  run-js-tests 12/12、loader 11/11、http 14/14・`--async` 17/17、font 8/8、html5lib tree 1854/1959・fragment 206/206。ASan の
  `--render`（top-local・search-local）に報告 0。
- Amazon（capture、`--run`）: Uncaught は top 2（async 1・for-of 1）、search 7（async 4・for-of 1・`(at 1:1)` 2）で p081 と同じ
  （DOM の側の Uncaught は無い）。`--run` の時間（host）: top 1.27 s → 1.37 s、search 1.86 s → 2.44 s（getComputedStyle が入り script が
  先まで進み、scrollWidth 等の強制 layout が増えた。box の索引の前は 6.24 s: `perf` で `page_node_box` の layout 全体の歩きが 85%）。
- `live-compare.py`（script 付き）: top-local 画素 64.47%・ink 57.66%、search-local 76.04%・ink 33.01%（不変）。
- guest（QEMU、worktree の image を作り直した。clang・libcxx を含まない config、browser の compile に warning 0）: run-dom-tests
  `--outputs` **16/16**。live の `https://www.amazon.co.jp/` の `--run`（取得 1 回）: Uncaught 0。
- boot test: PASS（worktree の `build/p081/boot-test82/login.png`）。
- 実機: 未実施。guest の窓（zdesktop）での scroll の操作: 未実施（view の scroll の移し替えは host の headless では通らない経路）。

## 写真

- host: worktree の `build/ws074-shots/p082-20260929-amazon-top-local-side.png`・`…-search-local-side.png`。

## 残り・制限

- 上の「この Phase に無いもの」。加えて: absolute・fixed の auto の inset の使われた値、`inline-flex`・`inline-grid` の display は
  engine が区別しないので "flex"・"grid"、background-image の URL は style sheet の書いたまま（Chromium は絶対 URL）、shorthand の
  property は ""。
- DOMException（`e.name`）は p083（main の指示、2026-09-29）。
- Amazon の画素の一致は不変: 差は script の残り（async・for-of: js の側）と layout の側。

## 結果

- cleared。Amazon の script で innerHTML の次に多い getComputedStyle・offset*・scroll* が入り、Chromium と一致する試験 1 つを足した。
  host（plain・ASan）と guest で全て通った。
