<!-- awesome-plan project=zedbsd record=ws074p082 -->

# ws074-p082: getComputedStyle、offsetTop・offsetLeft・offsetParent、scrollTo・scrollBy・scroll・scrollIntoView

Phase ID: `ws074-p082`
Parent: [WS074](../ws.md)
Status: in-progress
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

## Resume point

- 2026-09-29: 開始。
