<!-- awesome-plan project=zedbsd record=ws074p081 -->

# ws074-p081: HTML の断片の parse と直列化（innerHTML・outerHTML・insertAdjacentHTML）

Phase ID: `ws074-p081`
Parent: [WS074](../ws.md)
Status: in-progress
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行、2026-09-29）
依存: p005（tree builder）、p030（DOM の binding）、p031
並行: WS074 の別のエージェントが `js/`・`vm/` を進めている。この Phase は `html/`・`bind/` だけを変え、`js/`・`vm/` は変えない。

## 由来と順（2026-09-29 の調べ）

main の指示: p031 で入れなかった `getComputedStyle`・`innerHTML`・`outerHTML`・`insertAdjacentHTML`・`offsetTop`・`offsetLeft`・
`offsetParent`・`scrollTo`・`scrollIntoView` を、Amazon の表示に効く順に。Amazon の top と search の外の script（43 本、6.7 MB、私たちの
UA で取得、`build/p081/js/`）と inline script での使われ方:

| API | 外 | inline | 表示への効き方 |
| --- | --- | --- | --- |
| `innerHTML`（代入 144、読み 66） | 227 | 8 | 部品の中身を作る（carousel・popover・候補）。無いと中身が出ない |
| `getComputedStyle(` | 57 | 8 | 表示の判定・大きさの計算 |
| `outerHTML` | 51 | 0 | 部品の複製・置き換え |
| `offsetTop`・`offsetParent`・`offsetLeft` | 35・21・13 | 0 | popover・sticky の位置 |
| `scrollTo(`・`scrollIntoView(`・`scrollBy(` | 18・10・3 | 3 | carousel の送り（操作） |
| `insertAdjacentHTML` | 15 | 0 | 部品の差し込み |

順: 1. この Phase（p081）: HTML の断片の parse と直列化（`innerHTML`・`outerHTML`・`insertAdjacentHTML`・`insertAdjacentElement`・
`insertAdjacentText`）。2. 次の Phase（予定の p082）: `getComputedStyle`、`offsetTop`・`offsetLeft`・`offsetParent`、`scrollTo`・`scrollBy`・
`scroll`・`scrollIntoView`。

## 範囲

- `html/`: HTML の断片の parse（HTML Standard の「parsing HTML fragments」: context の要素による tokenizer の状態と insertion mode、
  root の html 要素、form の要素の pointer、template の mode）。html5lib の tree construction の fragment の case で確かめる
  （`host-tree` と `run-html5lib-tree.py` に fragment の case を足す）。
- 直列化（HTML Standard の「serializing HTML fragments」: 属性と text の escape、void 要素、raw text の要素、template の中身、
  外の namespace の名前）。
- `bind/`: `Element.innerHTML`・`outerHTML`（読み書き）、`insertAdjacentHTML`・`insertAdjacentElement`・`insertAdjacentText`、
  `DocumentFragment`・`ShadowRoot` は無い（`innerHTML` は Element だけ）。断片の中の script は走らない（他の browser と同じ）。

この Phase に無いもの: `DOMParser`、`Range.createContextualFragment`、`document.write`、XML の直列化。

## Resume point

実装（`html/parser.c` の断片の parse、直列化、`bind/`）と試験（html5lib の fragment、`plan/ws074/tests/dom/` に Chromium の expected）。
